#include "knox.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void run_cmd(const char *const argv[])
{
    tam_exec_result_t r = tam_exec(argv, 10000);
    if (r.exit_code != 0) {
        fprintf(stderr, "cmd failed (%d): %s\n", r.exit_code,
                r.stderr_buf ? r.stderr_buf : "");
    }
    assert(r.exit_code == 0);
    tam_exec_result_free(&r);
}

static void get_rev_parse(const char *repo_dir, const char *rev,
                          char *out, size_t cap)
{
    const char *argv[] = { "git", "-C", repo_dir, "rev-parse", rev, NULL };
    tam_exec_result_t r = tam_exec(argv, 10000);
    assert(r.exit_code == 0 && r.stdout_buf != NULL);
    snprintf(out, cap, "%s", r.stdout_buf);
    char *nl = strchr(out, '\n');
    if (nl)
        *nl = '\0';
    tam_exec_result_free(&r);
}

int main(void)
{
    char tmp_tpl[] = "/tmp/knox-git-test-XXXXXX";
    char *ws_dir = mkdtemp(tmp_tpl);
    assert(ws_dir != NULL);

    char *remote_dir = tam_path_join(ws_dir, "remote.git");
    char *repo_a = tam_path_join(ws_dir, "repo-alpha");
    char *repo_b = tam_path_join(ws_dir, "repo-beta");
    assert(tam_fs_mkdir_p(remote_dir, 0755));
    assert(tam_fs_mkdir_p(repo_a, 0755));

    /* 1. Initialize bare remote and repo-alpha on branch dev */
    run_cmd((const char *[]){"git", "-C", remote_dir, "init", "--bare", "-b", "dev", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "init", "-b", "dev", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "config", "user.name", "Test User", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "config", "user.email", "test@example.com", NULL});

    char *readme_a = tam_path_join(repo_a, "README.md");
    const char *init_md = "# Repo Alpha\n\nInitial text.\n";
    assert(tam_fs_write_atomic(readme_a, init_md, strlen(init_md), 0644));
    run_cmd((const char *[]){"git", "-C", repo_a, "add", "--", "README.md", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "commit", "-m", "Initial commit", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "remote", "add", "origin", remote_dir, NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "push", "-u", "origin", "dev", NULL});

    /* 2. Clone repo-beta from remote so we can test ahead/behind and sync */
    run_cmd((const char *[]){"git", "clone", remote_dir, repo_b, NULL});
    run_cmd((const char *[]){"git", "-C", repo_b, "config", "user.name", "Test User", NULL});
    run_cmd((const char *[]){"git", "-C", repo_b, "config", "user.email", "test@example.com", NULL});

    /* Modify repo-alpha: 1 modified file, 1 untracked file */
    const char *mod_md = "# Repo Alpha\n\nModified line.\n";
    assert(tam_fs_write_atomic(readme_a, mod_md, strlen(mod_md), 0644));
    char *notes_a = tam_path_join(repo_a, "notes.txt");
    assert(tam_fs_write_atomic(notes_a, "hello\n", 6, 0644));

    /* 3. Discover workspace repos */
    knox_workspace_t ws;
    knox_workspace_init(&ws, ws_dir);
    assert(knox_discover_repos(&ws, 2) == 0);
    assert(ws.count == 2);
    assert(strcmp(ws.repos[0].name, "repo-alpha") == 0);
    assert(strcmp(ws.repos[1].name, "repo-beta") == 0);

    knox_repo_t *ra = &ws.repos[0];
    knox_repo_t *rb = &ws.repos[1];
    assert(strcmp(ra->branch, "dev") == 0);
    assert(ra->dirty == true);
    assert(ra->change_count == 2);
    assert(rb->dirty == false);

    /* 4. Stage only README.md in repo-alpha and commit it */
    char err[256] = "";
    assert(knox_repo_stage_file(ra, "README.md", err, sizeof(err)) == 0);
    assert(ra->staged_count == 1);
    assert(ra->unstaged_count == 1); /* notes.txt remains untracked */

    char sum[256] = "";
    assert(knox_repo_commit_staged(ra, "Update README", sum, sizeof(sum)) == 0);
    assert(ra->ahead == 1);
    assert(ra->behind == 0);
    assert(ra->change_count == 1); /* notes.txt untouched by path-scoped commit! */

    /* 5. Sync repo-alpha (pushes 1 commit to remote), then sync repo-beta (pulls 1 commit) */
    assert(knox_repo_sync(ra, false, sum, sizeof(sum)) == 0);
    assert(ra->ahead == 0);
    assert(ra->behind == 0);
    char ra_head[64], ra_up[64], rb_head[64], rb_up[64];
    get_rev_parse(repo_a, "HEAD", ra_head, sizeof(ra_head));
    get_rev_parse(repo_a, "@{u}", ra_up, sizeof(ra_up));
    assert(strcmp(ra_head, ra_up) == 0);

    assert(knox_repo_sync(rb, false, sum, sizeof(sum)) == 0);
    assert(rb->ahead == 0);
    assert(rb->behind == 0);
    assert(strcmp(ra->head_hash, rb->head_hash) == 0);
    get_rev_parse(repo_b, "HEAD", rb_head, sizeof(rb_head));
    get_rev_parse(repo_b, "@{u}", rb_up, sizeof(rb_up));
    assert(strcmp(rb_head, rb_up) == 0);
    assert(strcmp(ra_head, rb_head) == 0);

    /* 5b. Protected branch push confirmation (main) */
    run_cmd((const char *[]){"git", "-C", repo_b, "checkout", "-b", "main", NULL});
    run_cmd((const char *[]){"git", "-C", repo_b, "push", "-u", "origin", "main", NULL});
    char *main_file = tam_path_join(repo_b, "main.txt");
    assert(tam_fs_write_atomic(main_file, "main update\n", 12, 0644));
    run_cmd((const char *[]){"git", "-C", repo_b, "add", "--", "main.txt", NULL});
    run_cmd((const char *[]){"git", "-C", repo_b, "commit", "-m", "Main commit", NULL});
    assert(knox_repo_refresh_status(rb) == 0);
    assert(rb->ahead == 1);
    assert(knox_repo_sync(rb, false, sum, sizeof(sum)) == 1);
    assert(strstr(sum, "Confirm push") != NULL);
    assert(knox_repo_sync(rb, true, sum, sizeof(sum)) == 0);
    assert(rb->ahead == 0 && rb->behind == 0);
    get_rev_parse(repo_b, "HEAD", rb_head, sizeof(rb_head));
    get_rev_parse(repo_b, "@{u}", rb_up, sizeof(rb_up));
    assert(strcmp(rb_head, rb_up) == 0);
    run_cmd((const char *[]){"git", "-C", repo_b, "checkout", "dev", NULL});
    assert(knox_repo_refresh_status(rb) == 0);
    free(main_file);

    /* 6. Verify commit details & diff loader */
    knox_commit_details_t det;
    assert(knox_repo_load_commit_details(ra, ra->head_hash, &det) == 0);
    assert(det.file_count == 1);
    assert(strcmp(det.files[0].path, "README.md") == 0);
    assert(det.files[0].additions == 1);
    assert(det.files[0].deletions == 1);
    knox_commit_details_free(&det);

    /* 7. Verify branch creation, listing, and checkout */
    assert(knox_repo_create_branch(ra, "feature-x", NULL, sum, sizeof(sum)) == 0);
    assert(strcmp(ra->branch, "feature-x") == 0);

    knox_branch_info_t *branches = NULL;
    size_t bcount = 0;
    assert(knox_repo_list_branches(ra, &branches, &bcount) == 0);
    assert(bcount >= 3); /* feature-x, dev, origin/dev */
    assert(branches[0].is_current == true);
    assert(strcmp(branches[0].name, "feature-x") == 0);
    knox_branch_list_free(branches, bcount);

    assert(knox_repo_checkout_branch(ra, "dev", false, sum, sizeof(sum)) == 0);
    assert(strcmp(ra->branch, "dev") == 0);

    /* 8. Verify stash push (including untracked notes.txt), list, diff, apply, and drop */
    assert(ra->dirty == true);
    assert(knox_repo_stash_push(ra, "WIP notes stash", true, sum, sizeof(sum)) == 0);
    assert(ra->dirty == false);

    knox_stash_entry_t *stashes = NULL;
    size_t scount = 0;
    assert(knox_repo_list_stashes(ra, &stashes, &scount) == 0);
    assert(scount == 1);
    assert(strcmp(stashes[0].ref, "stash@{0}") == 0);
    assert(strstr(stashes[0].message, "WIP notes stash") != NULL);

    char *sdiff = knox_repo_diff_stash(ra, "stash@{0}");
    assert(sdiff != NULL);
    assert(strstr(sdiff, "notes.txt") != NULL);
    free(sdiff);

    assert(knox_repo_stash_apply(ra, "stash@{0}", false, sum, sizeof(sum)) == 0);
    assert(ra->dirty == true);
    assert(knox_repo_stash_drop(ra, "stash@{0}", sum, sizeof(sum)) == 0);
    knox_stash_list_free(stashes, scount);

    assert(knox_repo_list_stashes(ra, &stashes, &scount) == 0);
    assert(scount == 0);
    knox_stash_list_free(stashes, scount);

    knox_workspace_free(&ws);
    free(notes_a);
    free(readme_a);
    free(repo_b);
    free(repo_a);
    free(remote_dir);
    tam_fs_remove_tree(-100 /* AT_FDCWD */, ws_dir);

    printf("test-git: all tests passed\n");
    return 0;
}
