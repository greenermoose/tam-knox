#include "knox.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GIT_FIELD_SEP "\x1f"
#define GIT_DEFAULT_TIMEOUT_MS 15000
#define GIT_NET_TIMEOUT_MS     60000

static char *xstrdup(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy)
        memcpy(copy, s, len + 1);
    return copy;
}

void knox_repo_free_changes(knox_repo_t *repo)
{
    if (!repo)
        return;
    for (size_t i = 0; i < repo->change_count; i++) {
        free(repo->changes[i].path);
        free(repo->changes[i].old_path);
    }
    free(repo->changes);
    repo->changes = NULL;
    repo->change_count = 0;
    repo->change_cap = 0;
    repo->staged_count = 0;
    repo->unstaged_count = 0;
    repo->dirty = false;
}

void knox_repo_free(knox_repo_t *repo)
{
    if (!repo)
        return;
    free(repo->name);
    free(repo->root_path);
    free(repo->rel_path);
    free(repo->branch);
    free(repo->upstream);
    free(repo->last_sync_msg);
    knox_repo_free_changes(repo);
    *repo = (knox_repo_t){0};
}

void knox_workspace_init(knox_workspace_t *ws, const char *root_path)
{
    *ws = (knox_workspace_t){0};
    char resolved[4096];
    if (root_path && realpath(root_path, resolved))
        ws->root_path = xstrdup(resolved);
    else
        ws->root_path = xstrdup(root_path ? root_path : ".");
}

void knox_workspace_free(knox_workspace_t *ws)
{
    if (!ws)
        return;
    for (size_t i = 0; i < ws->count; i++)
        knox_repo_free(&ws->repos[i]);
    free(ws->repos);
    free(ws->root_path);
    *ws = (knox_workspace_t){0};
}

static bool is_git_root_dir(const char *dir_path)
{
    char *git_entry = tam_path_join(dir_path, ".git");
    if (!git_entry)
        return false;
    struct stat st;
    bool ok = (stat(git_entry, &st) == 0 &&
               (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)));
    free(git_entry);
    return ok;
}

static bool ws_add_repo(knox_workspace_t *ws, const char *abs_path)
{
    for (size_t i = 0; i < ws->count; i++) {
        if (strcmp(ws->repos[i].root_path, abs_path) == 0)
            return true;
    }
    if (ws->count == ws->cap) {
        size_t next_cap = ws->cap ? ws->cap * 2 : 16;
        knox_repo_t *grown = realloc(ws->repos, next_cap * sizeof(*grown));
        if (!grown)
            return false;
        ws->repos = grown;
        ws->cap = next_cap;
    }
    knox_repo_t *r = &ws->repos[ws->count++];
    *r = (knox_repo_t){0};
    r->root_path = xstrdup(abs_path);

    const char *slash = strrchr(abs_path, '/');
    r->name = xstrdup((slash && slash[1]) ? slash + 1 : abs_path);

    size_t root_len = strlen(ws->root_path);
    if (strncmp(abs_path, ws->root_path, root_len) == 0 &&
        abs_path[root_len] == '/') {
        r->rel_path = xstrdup(abs_path + root_len + 1);
    } else {
        r->rel_path = xstrdup(".");
    }
    r->branch = xstrdup("(unknown)");
    return true;
}

static void scan_dir_for_repos(knox_workspace_t *ws, const char *dir_path,
                               int depth_left)
{
    if (is_git_root_dir(dir_path)) {
        ws_add_repo(ws, dir_path);
        /* Still check children at top root level in case a meta-repo wraps sub-repos */
        if (strcmp(dir_path, ws->root_path) != 0)
            return;
    }
    if (depth_left <= 0)
        return;

    DIR *d = opendir(dir_path);
    if (!d)
        return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.' ||
            strcmp(name, "build") == 0 ||
            strcmp(name, "node_modules") == 0 ||
            strcmp(name, "worktrees") == 0)
            continue;
        char *child = tam_path_join(dir_path, name);
        if (!child)
            continue;
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
            scan_dir_for_repos(ws, child, depth_left - 1);
        free(child);
    }
    closedir(d);
}

static int cmp_repos_by_name(const void *a, const void *b)
{
    const knox_repo_t *ra = (const knox_repo_t *)a;
    const knox_repo_t *rb = (const knox_repo_t *)b;
    return strcmp(ra->name, rb->name);
}

static bool repo_push_change(knox_repo_t *repo, const char *path,
                             const char *old_path,
                             knox_change_type_t idx_st,
                             knox_change_type_t wt_st)
{
    if (repo->change_count == repo->change_cap) {
        size_t next = repo->change_cap ? repo->change_cap * 2 : 16;
        knox_file_change_t *grown = realloc(repo->changes,
                                            next * sizeof(*grown));
        if (!grown)
            return false;
        repo->changes = grown;
        repo->change_cap = next;
    }
    knox_file_change_t *c = &repo->changes[repo->change_count++];
    *c = (knox_file_change_t){
        .path = xstrdup(path),
        .old_path = old_path ? xstrdup(old_path) : NULL,
        .index_status = idx_st,
        .worktree_status = wt_st,
        .staged = (idx_st != KNOX_CHANGE_NONE && idx_st != KNOX_CHANGE_UNTRACKED),
        .unstaged = (wt_st != KNOX_CHANGE_NONE),
        .additions = -1,
        .deletions = -1
    };
    if (c->staged)
        repo->staged_count++;
    if (c->unstaged)
        repo->unstaged_count++;
    repo->dirty = true;
    return true;
}

static knox_change_type_t map_porcelain_char(char ch)
{
    switch (ch) {
    case 'M': return KNOX_CHANGE_MODIFIED;
    case 'A': return KNOX_CHANGE_ADDED;
    case 'D': return KNOX_CHANGE_DELETED;
    case 'R': return KNOX_CHANGE_RENAMED;
    case 'C': return KNOX_CHANGE_COPIED;
    case 'U': return KNOX_CHANGE_CONFLICT;
    case '?': return KNOX_CHANGE_UNTRACKED;
    default:  return KNOX_CHANGE_NONE;
    }
}

int knox_repo_refresh_status(knox_repo_t *repo)
{
    if (!repo || !repo->root_path)
        return -1;

    knox_repo_free_changes(repo);
    repo->ahead = 0;
    repo->behind = 0;
    repo->head_hash[0] = '\0';
    free(repo->upstream);
    repo->upstream = NULL;

    const char *argv[] = {
        "git", "-C", repo->root_path,
        "status", "--porcelain=v2", "--branch", "--untracked-files=all", "-z",
        NULL
    };
    tam_exec_result_t res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
    if (res.exit_code != 0 || !res.stdout_buf) {
        tam_exec_result_free(&res);
        return -1;
    }

    const char *p = res.stdout_buf;
    const char *end = res.stdout_buf + res.stdout_len;
    while (p < end) {
        size_t len = strnlen(p, (size_t)(end - p));
        if (len == 0) {
            p++;
            continue;
        }
        if (p[0] == '#') {
            if (strncmp(p, "# branch.oid ", 13) == 0) {
                const char *oid = p + 13;
                if (strncmp(oid, "(initial)", 9) != 0) {
                    snprintf(repo->head_hash, sizeof(repo->head_hash),
                             "%.40s", oid);
                }
            } else if (strncmp(p, "# branch.head ", 14) == 0) {
                free(repo->branch);
                repo->branch = xstrdup(p + 14);
            } else if (strncmp(p, "# branch.upstream ", 18) == 0) {
                free(repo->upstream);
                repo->upstream = xstrdup(p + 18);
            } else if (strncmp(p, "# branch.ab ", 12) == 0) {
                int a = 0, b = 0;
                if (sscanf(p + 12, "+%d -%d", &a, &b) == 2) {
                    repo->ahead = a;
                    repo->behind = abs(b);
                }
            }
            p += len + 1;
        } else if (p[0] == '1' && len > 2) {
            /* 1 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <path> */
            char x = p[2];
            char y = p[3];
            const char *tok = p;
            int spaces = 0;
            while (*tok && spaces < 8) {
                if (*tok == ' ')
                    spaces++;
                tok++;
            }
            if (spaces == 8 && *tok) {
                repo_push_change(repo, tok, NULL,
                                 map_porcelain_char(x),
                                 map_porcelain_char(y));
            }
            p += len + 1;
        } else if (p[0] == '2' && len > 2) {
            /* 2 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <X><score> <path>\0<origPath>\0 */
            char x = p[2];
            char y = p[3];
            const char *tok = p;
            int spaces = 0;
            while (*tok && spaces < 9) {
                if (*tok == ' ')
                    spaces++;
                tok++;
            }
            const char *new_path = (spaces == 9) ? tok : "";
            p += len + 1;
            const char *old_path = NULL;
            if (p < end) {
                size_t olen = strnlen(p, (size_t)(end - p));
                old_path = p;
                p += olen + 1;
            }
            repo_push_change(repo, new_path, old_path,
                             map_porcelain_char(x),
                             map_porcelain_char(y));
        } else if (p[0] == 'u' && len > 2) {
            /* u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path> */
            const char *tok = p;
            int spaces = 0;
            while (*tok && spaces < 10) {
                if (*tok == ' ')
                    spaces++;
                tok++;
            }
            if (spaces == 10 && *tok) {
                repo_push_change(repo, tok, NULL,
                                 KNOX_CHANGE_CONFLICT,
                                 KNOX_CHANGE_CONFLICT);
            }
            p += len + 1;
        } else if (p[0] == '?' && len > 2) {
            /* ? <path> */
            repo_push_change(repo, p + 2, NULL,
                             KNOX_CHANGE_NONE,
                             KNOX_CHANGE_UNTRACKED);
            p += len + 1;
        } else {
            p += len + 1;
        }
    }

    tam_exec_result_free(&res);
    return 0;
}

int knox_discover_repos(knox_workspace_t *ws, int max_depth)
{
    if (!ws || !ws->root_path)
        return -1;
    for (size_t i = 0; i < ws->count; i++)
        knox_repo_free(&ws->repos[i]);
    ws->count = 0;

    scan_dir_for_repos(ws, ws->root_path, max_depth);
    if (ws->count > 1)
        qsort(ws->repos, ws->count, sizeof(knox_repo_t), cmp_repos_by_name);

    for (size_t i = 0; i < ws->count; i++)
        knox_repo_refresh_status(&ws->repos[i]);

    return 0;
}

knox_repo_t *knox_workspace_find_repo_for_path(knox_workspace_t *ws,
                                               const char *abs_path)
{
    if (!ws || !abs_path)
        return NULL;
    knox_repo_t *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < ws->count; i++) {
        const char *rp = ws->repos[i].root_path;
        size_t rlen = strlen(rp);
        if (strncmp(abs_path, rp, rlen) == 0 &&
            (abs_path[rlen] == '\0' || abs_path[rlen] == '/')) {
            if (rlen > best_len) {
                best = &ws->repos[i];
                best_len = rlen;
            }
        }
    }
    return best;
}

bool knox_branch_needs_push_confirm(const char *branch)
{
    if (!branch)
        return false;
    return strcmp(branch, "main") == 0 ||
           strcmp(branch, "master") == 0 ||
           strcmp(branch, "test") == 0;
}

int knox_repo_stage_file(knox_repo_t *repo, const char *rel_path,
                         char *errbuf, size_t errcap)
{
    if (!repo || !rel_path || !*rel_path)
        return -1;
    const char *argv[] = {
        "git", "-C", repo->root_path, "add", "--", rel_path, NULL
    };
    tam_exec_result_t res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
    int rc = (res.exit_code == 0) ? 0 : -1;
    if (rc != 0 && errbuf && errcap > 0) {
        snprintf(errbuf, errcap, "%s",
                 (res.stderr_buf && *res.stderr_buf) ? res.stderr_buf : "git add failed");
    }
    tam_exec_result_free(&res);
    if (rc == 0)
        knox_repo_refresh_status(repo);
    return rc;
}

int knox_repo_unstage_file(knox_repo_t *repo, const char *rel_path,
                           char *errbuf, size_t errcap)
{
    if (!repo || !rel_path || !*rel_path)
        return -1;
    tam_exec_result_t res;
    if (repo->head_hash[0] != '\0') {
        const char *argv[] = {
            "git", "-C", repo->root_path, "reset", "HEAD", "--", rel_path, NULL
        };
        res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
    } else {
        const char *argv[] = {
            "git", "-C", repo->root_path, "rm", "--cached", "--", rel_path, NULL
        };
        res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
    }
    int rc = (res.exit_code == 0) ? 0 : -1;
    if (rc != 0 && errbuf && errcap > 0) {
        snprintf(errbuf, errcap, "%s",
                 (res.stderr_buf && *res.stderr_buf) ? res.stderr_buf : "git unstage failed");
    }
    tam_exec_result_free(&res);
    if (rc == 0)
        knox_repo_refresh_status(repo);
    return rc;
}

int knox_repo_commit_staged(knox_repo_t *repo, const char *message,
                            char *out_summary, size_t sumcap)
{
    if (!repo || !message || !*message) {
        if (out_summary && sumcap > 0)
            snprintf(out_summary, sumcap, "Commit message cannot be empty");
        return -1;
    }
    if (repo->staged_count == 0) {
        if (out_summary && sumcap > 0)
            snprintf(out_summary, sumcap, "No staged files to commit (press Space on a file to stage)");
        return -1;
    }

    /* Strictly commit only the explicitly staged paths (Tamlinux concurrency rule). */
    size_t max_args = 8 + repo->change_count * 2;
    const char **argv = calloc(max_args, sizeof(char *));
    if (!argv)
        return -1;

    size_t k = 0;
    argv[k++] = "git";
    argv[k++] = "-C";
    argv[k++] = repo->root_path;
    argv[k++] = "commit";
    argv[k++] = "-m";
    argv[k++] = message;
    argv[k++] = "--";
    for (size_t i = 0; i < repo->change_count; i++) {
        if (repo->changes[i].staged) {
            argv[k++] = repo->changes[i].path;
            if (repo->changes[i].old_path)
                argv[k++] = repo->changes[i].old_path;
        }
    }
    argv[k] = NULL;

    tam_exec_result_t res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
    free(argv);

    int rc = (res.exit_code == 0) ? 0 : -1;
    if (out_summary && sumcap > 0) {
        const char *msg = (res.stdout_buf && *res.stdout_buf) ? res.stdout_buf :
                          (res.stderr_buf && *res.stderr_buf) ? res.stderr_buf :
                          (rc == 0 ? "Committed staged changes" : "Commit failed");
        snprintf(out_summary, sumcap, "%s", msg);
        char *nl = strchr(out_summary, '\n');
        if (nl)
            *nl = '\0';
    }
    free(repo->last_sync_msg);
    repo->last_sync_msg = (out_summary && sumcap > 0) ? xstrdup(out_summary) : NULL;
    tam_exec_result_free(&res);
    knox_repo_refresh_status(repo);
    return rc;
}

int knox_repo_sync(knox_repo_t *repo, bool allow_protected_push,
                   char *out_summary, size_t sumcap)
{
    if (!repo || !repo->root_path)
        return -1;

    /* 1. Check if remote exists */
    const char *rem_argv[] = { "git", "-C", repo->root_path, "remote", NULL };
    tam_exec_result_t rem_res = tam_exec(rem_argv, GIT_DEFAULT_TIMEOUT_MS);
    bool has_remote = (rem_res.exit_code == 0 && rem_res.stdout_buf &&
                       rem_res.stdout_buf[0] != '\0');
    tam_exec_result_free(&rem_res);
    if (!has_remote) {
        if (out_summary && sumcap > 0)
            snprintf(out_summary, sumcap, "%s: no remote configured", repo->name);
        return -1;
    }

    /* 2. Protected branch check before pushing main or test */
    if (!allow_protected_push && repo->ahead > 0 &&
        knox_branch_needs_push_confirm(repo->branch)) {
        if (out_summary && sumcap > 0) {
            snprintf(out_summary, sumcap,
                     "Confirm push of %d commit(s) to protected branch '%s' in %s? (y/N)",
                     repo->ahead, repo->branch, repo->name);
        }
        return 1; /* Needs user confirmation */
    }

    /* 3. Fetch */
    const char *fetch_argv[] = {
        "git", "-C", repo->root_path, "fetch", "--prune", NULL
    };
    tam_exec_result_t fetch_res = tam_exec(fetch_argv, GIT_NET_TIMEOUT_MS);
    if (fetch_res.exit_code != 0) {
        if (out_summary && sumcap > 0) {
            snprintf(out_summary, sumcap, "%s: fetch failed: %s",
                     repo->name,
                     fetch_res.stderr_buf ? fetch_res.stderr_buf : "error");
            char *nl = strchr(out_summary, '\n');
            if (nl) *nl = '\0';
        }
        tam_exec_result_free(&fetch_res);
        return -1;
    }
    tam_exec_result_free(&fetch_res);
    knox_repo_refresh_status(repo);

    /* Re-check protected branch after fetch in case ahead > 0 */
    if (!allow_protected_push && repo->ahead > 0 &&
        knox_branch_needs_push_confirm(repo->branch)) {
        if (out_summary && sumcap > 0) {
            snprintf(out_summary, sumcap,
                     "Confirm push of %d commit(s) to protected branch '%s' in %s? (y/N)",
                     repo->ahead, repo->branch, repo->name);
        }
        return 1;
    }

    int pulled = 0;
    int pushed = 0;

    /* 4. Fast-forward pull if behind */
    if (repo->behind > 0) {
        int prev_behind = repo->behind;
        const char *pull_argv[] = {
            "git", "-C", repo->root_path, "pull", "--ff-only", NULL
        };
        tam_exec_result_t pull_res = tam_exec(pull_argv, GIT_NET_TIMEOUT_MS);
        if (pull_res.exit_code != 0) {
            if (out_summary && sumcap > 0) {
                snprintf(out_summary, sumcap, "%s: pull --ff-only failed: %s",
                         repo->name,
                         pull_res.stderr_buf ? pull_res.stderr_buf : "diverged");
                char *nl = strchr(out_summary, '\n');
                if (nl) *nl = '\0';
            }
            tam_exec_result_free(&pull_res);
            knox_repo_refresh_status(repo);
            return -1;
        }
        tam_exec_result_free(&pull_res);
        pulled = prev_behind;
        knox_repo_refresh_status(repo);
    }

    /* 5. Push if ahead */
    if (repo->ahead > 0) {
        int prev_ahead = repo->ahead;
        const char *push_argv[] = {
            "git", "-C", repo->root_path, "push", NULL
        };
        tam_exec_result_t push_res = tam_exec(push_argv, GIT_NET_TIMEOUT_MS);
        if (push_res.exit_code != 0) {
            if (out_summary && sumcap > 0) {
                snprintf(out_summary, sumcap, "%s: push failed: %s",
                         repo->name,
                         push_res.stderr_buf ? push_res.stderr_buf : "error");
                char *nl = strchr(out_summary, '\n');
                if (nl) *nl = '\0';
            }
            tam_exec_result_free(&push_res);
            knox_repo_refresh_status(repo);
            return -1;
        }
        tam_exec_result_free(&push_res);
        pushed = prev_ahead;
        knox_repo_refresh_status(repo);
    }

    if (out_summary && sumcap > 0) {
        if (pulled > 0 && pushed > 0)
            snprintf(out_summary, sumcap, "%s (%s): synced (pulled %d, pushed %d)",
                     repo->name, repo->branch, pulled, pushed);
        else if (pulled > 0)
            snprintf(out_summary, sumcap, "%s (%s): pulled %d commit(s)",
                     repo->name, repo->branch, pulled);
        else if (pushed > 0)
            snprintf(out_summary, sumcap, "%s (%s): pushed %d commit(s)",
                     repo->name, repo->branch, pushed);
        else
            snprintf(out_summary, sumcap, "%s (%s): up to date with remote",
                     repo->name, repo->branch);
    }
    free(repo->last_sync_msg);
    repo->last_sync_msg = (out_summary && sumcap > 0) ? xstrdup(out_summary) : NULL;
    return 0;
}

char *knox_repo_diff_working_file(const knox_repo_t *repo,
                                  const knox_file_change_t *chg)
{
    if (!repo || !chg || !chg->path)
        return NULL;

    if (chg->worktree_status == KNOX_CHANGE_UNTRACKED) {
        char *abs_file = tam_path_join(repo->root_path, chg->path);
        size_t flen = 0;
        char *raw = abs_file ? tam_fs_read_file(abs_file, &flen) : NULL;
        free(abs_file);
        if (!raw)
            return xstrdup("(Untracked binary or unreadable file)\n");

        tam_str_t s = tam_str_new("");
        s = tam_str_cat_printf(s, "--- /dev/null\n+++ b/%s\n@@ -0,0 +1 @@\n",
                               chg->path);
        const char *line = raw;
        while (*line) {
            const char *nl = strchr(line, '\n');
            size_t l = nl ? (size_t)(nl - line + 1) : strlen(line);
            s = tam_str_cat(s, "+");
            s = tam_str_cat_len(s, line, l);
            if (!nl) {
                s = tam_str_cat(s, "\n");
                break;
            }
            line = nl + 1;
        }
        free(raw);
        char *out = xstrdup(s);
        tam_str_free(s);
        return out;
    }

    const char *argv_head[] = {
        "git", "-C", repo->root_path, "--no-pager",
        "diff", "HEAD", "--", chg->path, NULL
    };
    tam_exec_result_t res = tam_exec(argv_head, GIT_DEFAULT_TIMEOUT_MS);
    if (res.exit_code == 0 && res.stdout_buf && res.stdout_len > 0) {
        char *out = xstrdup(res.stdout_buf);
        tam_exec_result_free(&res);
        return out;
    }
    tam_exec_result_free(&res);

    const char *argv_cached[] = {
        "git", "-C", repo->root_path, "--no-pager",
        "diff", "--cached", "--", chg->path, NULL
    };
    res = tam_exec(argv_cached, GIT_DEFAULT_TIMEOUT_MS);
    char *out = (res.stdout_buf && res.stdout_len > 0)
                ? xstrdup(res.stdout_buf)
                : xstrdup("(No diff output)\n");
    tam_exec_result_free(&res);
    return out;
}

char *knox_repo_diff_commit_file(const knox_repo_t *repo,
                                 const char *commit_hash,
                                 const char *rel_path)
{
    if (!repo || !commit_hash || !*commit_hash)
        return NULL;

    if (strcmp(commit_hash, KNOX_UNCOMMITTED_HASH) == 0) {
        if (rel_path) {
            for (size_t i = 0; i < repo->change_count; i++) {
                if (strcmp(repo->changes[i].path, rel_path) == 0)
                    return knox_repo_diff_working_file(repo, &repo->changes[i]);
            }
        }
        const char *argv[] = {
            "git", "-C", repo->root_path, "--no-pager", "diff", "HEAD", NULL
        };
        tam_exec_result_t res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
        char *out = (res.stdout_buf && *res.stdout_buf)
                    ? xstrdup(res.stdout_buf)
                    : xstrdup("(No uncommitted diff output)\n");
        tam_exec_result_free(&res);
        return out;
    }

    const char *argv_file[] = {
        "git", "-C", repo->root_path, "-c", "log.showSignature=false",
        "--no-pager", "show", "--format=fuller", "--stat", "-p",
        commit_hash, "--", rel_path ? rel_path : "", NULL
    };
    if (!rel_path || !*rel_path)
        argv_file[11] = NULL;
    tam_exec_result_t res = tam_exec(argv_file, GIT_DEFAULT_TIMEOUT_MS);
    char *out = (res.stdout_buf && *res.stdout_buf)
                ? xstrdup(res.stdout_buf)
                : xstrdup("(No commit diff available)\n");
    tam_exec_result_free(&res);
    return out;
}

char *knox_repo_show_commit_file(const knox_repo_t *repo,
                                 const char *commit_hash,
                                 const char *rel_path)
{
    if (!repo || !commit_hash || !rel_path)
        return NULL;
    if (strcmp(commit_hash, KNOX_UNCOMMITTED_HASH) == 0) {
        char *abs_file = tam_path_join(repo->root_path, rel_path);
        size_t len = 0;
        char *txt = abs_file ? tam_fs_read_file(abs_file, &len) : NULL;
        free(abs_file);
        return txt;
    }
    char spec[1024];
    snprintf(spec, sizeof(spec), "%s:%s", commit_hash, rel_path);
    const char *argv[] = {
        "git", "-C", repo->root_path, "--no-pager", "show", spec, NULL
    };
    tam_exec_result_t res = tam_exec(argv, GIT_DEFAULT_TIMEOUT_MS);
    char *out = (res.exit_code == 0 && res.stdout_buf)
                ? xstrdup(res.stdout_buf)
                : NULL;
    tam_exec_result_free(&res);
    return out;
}

int knox_repo_load_commit_details(const knox_repo_t *repo,
                                  const char *commit_hash,
                                  knox_commit_details_t *out)
{
    if (!repo || !commit_hash || !out)
        return -1;
    *out = (knox_commit_details_t){0};
    snprintf(out->hash, sizeof(out->hash), "%.40s", commit_hash);

    if (strcmp(commit_hash, KNOX_UNCOMMITTED_HASH) == 0) {
        out->author = xstrdup("Working Tree");
        out->author_email = xstrdup("");
        out->committer = xstrdup("Working Tree");
        out->committer_email = xstrdup("");
        out->author_date = (int64_t)time(NULL);
        out->committer_date = out->author_date;
        out->body = xstrdup("Uncommitted working-tree and staged changes.");
        if (repo->change_count > 0) {
            out->files = calloc(repo->change_count, sizeof(knox_file_change_t));
            if (out->files) {
                out->file_count = repo->change_count;
                for (size_t i = 0; i < repo->change_count; i++) {
                    out->files[i] = repo->changes[i];
                    out->files[i].path = xstrdup(repo->changes[i].path);
                    out->files[i].old_path = repo->changes[i].old_path
                                             ? xstrdup(repo->changes[i].old_path)
                                             : NULL;
                }
            }
        }
        return 0;
    }

    /* 1. Metadata & body via git show --quiet */
    const char *fmt = "--format=%H" GIT_FIELD_SEP "%P" GIT_FIELD_SEP
                      "%an" GIT_FIELD_SEP "%ae" GIT_FIELD_SEP "%at" GIT_FIELD_SEP
                      "%cn" GIT_FIELD_SEP "%ce" GIT_FIELD_SEP "%ct" GIT_FIELD_SEP "%B";
    const char *meta_argv[] = {
        "git", "-C", repo->root_path, "-c", "log.showSignature=false",
        "show", "--quiet", fmt, commit_hash, "--", NULL
    };
    tam_exec_result_t meta_res = tam_exec(meta_argv, GIT_DEFAULT_TIMEOUT_MS);
    if (meta_res.exit_code != 0 || !meta_res.stdout_buf) {
        tam_exec_result_free(&meta_res);
        return -1;
    }

    char *buf = meta_res.stdout_buf;
    char *fields[9] = {0};
    size_t nf = 0;
    char *cur = buf;
    while (nf < 8) {
        char *sep = strchr(cur, '\x1f');
        if (!sep)
            break;
        *sep = '\0';
        fields[nf++] = cur;
        cur = sep + 1;
    }
    fields[nf++] = cur;

    if (nf == 9) {
        snprintf(out->hash, sizeof(out->hash), "%.40s", fields[0]);
        out->author = xstrdup(fields[2]);
        out->author_email = xstrdup(fields[3]);
        out->author_date = strtoll(fields[4], NULL, 10);
        out->committer = xstrdup(fields[5]);
        out->committer_email = xstrdup(fields[6]);
        out->committer_date = strtoll(fields[7], NULL, 10);
        size_t blen = strlen(fields[8]);
        while (blen > 0 && (fields[8][blen - 1] == '\n' || fields[8][blen - 1] == '\r'))
            fields[8][--blen] = '\0';
        out->body = xstrdup(fields[8]);
    }
    tam_exec_result_free(&meta_res);

    /* 2. Changed files via git diff-tree --name-status -r --root -M -z */
    const char *ns_argv[] = {
        "git", "-C", repo->root_path, "diff-tree", "--no-commit-id",
        "--name-status", "-r", "--root", "-M", "-z", commit_hash, NULL
    };
    tam_exec_result_t ns_res = tam_exec(ns_argv, GIT_DEFAULT_TIMEOUT_MS);
    size_t cap = 0;
    if (ns_res.exit_code == 0 && ns_res.stdout_buf) {
        const char *p = ns_res.stdout_buf;
        const char *end = ns_res.stdout_buf + ns_res.stdout_len;
        while (p < end && *p) {
            const char *status_tok = p;
            p += strlen(p) + 1;
            if (p >= end)
                break;
            char st_ch = status_tok[0];
            const char *path1 = p;
            p += strlen(p) + 1;
            const char *path2 = NULL;
            if ((st_ch == 'R' || st_ch == 'C') && p < end) {
                path2 = p;
                p += strlen(p) + 1;
            }
            if (out->file_count == cap) {
                size_t next = cap ? cap * 2 : 16;
                knox_file_change_t *grown = realloc(out->files,
                                                    next * sizeof(*grown));
                if (!grown)
                    break;
                out->files = grown;
                cap = next;
            }
            knox_file_change_t *fc = &out->files[out->file_count++];
            *fc = (knox_file_change_t){
                .path = xstrdup(path2 ? path2 : path1),
                .old_path = path2 ? xstrdup(path1) : NULL,
                .index_status = map_porcelain_char(st_ch),
                .worktree_status = KNOX_CHANGE_NONE,
                .staged = true,
                .additions = -1,
                .deletions = -1
            };
        }
    }
    tam_exec_result_free(&ns_res);

    /* 3. Additions & deletions via git diff-tree --numstat -r --root -M -z */
    const char *num_argv[] = {
        "git", "-C", repo->root_path, "diff-tree", "--no-commit-id",
        "--numstat", "-r", "--root", "-M", "-z", commit_hash, NULL
    };
    tam_exec_result_t num_res = tam_exec(num_argv, GIT_DEFAULT_TIMEOUT_MS);
    if (num_res.exit_code == 0 && num_res.stdout_buf) {
        const char *p = num_res.stdout_buf;
        const char *end = num_res.stdout_buf + num_res.stdout_len;
        size_t idx = 0;
        while (p < end && *p && idx < out->file_count) {
            const char *rec = p;
            p += strlen(p) + 1;
            int add = -1, del = -1;
            const char *tab1 = strchr(rec, '\t');
            const char *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
            if (tab1 && tab2) {
                if (rec[0] != '-')
                    add = atoi(rec);
                if (tab1[1] != '-')
                    del = atoi(tab1 + 1);
                if (tab2[1] == '\0') {
                    /* Renamed entry in -z numstat puts old\0new\0 next */
                    if (p < end) p += strlen(p) + 1;
                    if (p < end) p += strlen(p) + 1;
                }
            }
            out->files[idx].additions = add;
            out->files[idx].deletions = del;
            idx++;
        }
    }
    tam_exec_result_free(&num_res);
    return 0;
}

void knox_commit_details_free(knox_commit_details_t *d)
{
    if (!d)
        return;
    free(d->author);
    free(d->author_email);
    free(d->committer);
    free(d->committer_email);
    free(d->body);
    for (size_t i = 0; i < d->file_count; i++) {
        free(d->files[i].path);
        free(d->files[i].old_path);
    }
    free(d->files);
    *d = (knox_commit_details_t){0};
}
