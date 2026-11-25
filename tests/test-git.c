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
    assert(knox_repo_sync(rb, false, sum, sizeof(sum)) == 0);
    assert(rb->behind == 0);
    assert(strcmp(ra->head_hash, rb->head_hash) == 0);

    /* 6. Verify commit details & diff loader */
    knox_commit_details_t det;
    assert(knox_repo_load_commit_details(ra, ra->head_hash, &det) == 0);
    assert(det.file_count == 1);
    assert(strcmp(det.files[0].path, "README.md") == 0);
    assert(det.files[0].additions == 1);
    assert(det.files[0].deletions == 1);
    knox_commit_details_free(&det);

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
