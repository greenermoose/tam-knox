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

static bool flat_contains_name(const knox_explorer_t *ex, const char *name)
{
    for (size_t i = 0; i < ex->flat_count; i++) {
        if (ex->flat[i]->name && strcmp(ex->flat[i]->name, name) == 0)
            return true;
    }
    return false;
}

static void test_explorer_filter(void)
{
    char tpl[] = "/tmp/knox-exp-XXXXXX";
    char *root = mkdtemp(tpl);
    assert(root != NULL);

    char *src_dir = tam_path_join(root, "src");
    char *docs_dir = tam_path_join(root, "docs");
    char *build_dir = tam_path_join(root, "build");
    assert(tam_fs_mkdir_p(src_dir, 0755));
    assert(tam_fs_mkdir_p(docs_dir, 0755));
    assert(tam_fs_mkdir_p(build_dir, 0755));

    char *f1 = tam_path_join(src_dir, "main.c");
    char *f2 = tam_path_join(src_dir, "keymap.c");
    char *f3 = tam_path_join(docs_dir, "guide.md");
    char *f4 = tam_path_join(build_dir, "keymap.o");
    assert(tam_fs_write_atomic(f1, "int main(void){return 0;}\n", 26, 0644));
    assert(tam_fs_write_atomic(f2, "/* keymap */\n", 13, 0644));
    assert(tam_fs_write_atomic(f3, "# Guide\n", 8, 0644));
    assert(tam_fs_write_atomic(f4, "obj\n", 4, 0644));

    knox_explorer_t ex;
    assert(knox_explorer_init(&ex, root, false) == 0);
    size_t initial_count = ex.flat_count;
    assert(initial_count == 4); /* root + build + docs + src (collapsed) */
    assert(!flat_contains_name(&ex, "keymap.c"));

    /* Filter finds nested file inside collapsed src/ and skips build/ */
    assert(knox_explorer_set_filter(&ex, "KEYMAP", NULL) == 0);
    assert(flat_contains_name(&ex, "src"));
    assert(flat_contains_name(&ex, "keymap.c"));
    assert(!flat_contains_name(&ex, "main.c"));
    assert(!flat_contains_name(&ex, "guide.md"));
    assert(!flat_contains_name(&ex, "keymap.o"));

    /* Relative-path substring match */
    assert(knox_explorer_set_filter(&ex, "docs/guide", NULL) == 0);
    assert(flat_contains_name(&ex, "docs"));
    assert(flat_contains_name(&ex, "guide.md"));
    assert(!flat_contains_name(&ex, "src"));

    /* Clearing filter restores exact original collapsed tree state */
    knox_explorer_clear_filter(&ex);
    assert(ex.filter[0] == '\0');
    assert(ex.flat_count == initial_count);
    assert(!flat_contains_name(&ex, "keymap.c"));

    knox_explorer_free(&ex);
    free(f4);
    free(f3);
    free(f2);
    free(f1);
    free(build_dir);
    free(docs_dir);
    free(src_dir);
    tam_fs_remove_tree(-100, root);
}

static void test_explorer_reveal(void)
{
    char tpl[] = "/tmp/knox-rev-XXXXXX";
    char *root = mkdtemp(tpl);
    assert(root != NULL);

    char *repo_a = tam_path_join(root, "repo-a");
    char *nested_parent = tam_path_join(root, "sub");
    char *repo_b = tam_path_join(nested_parent, "repo-b");
    assert(tam_fs_mkdir_p(repo_a, 0755));
    assert(tam_fs_mkdir_p(repo_b, 0755));

    char *fa = tam_path_join(repo_a, "alpha.txt");
    char *fb = tam_path_join(repo_b, "beta.txt");
    assert(tam_fs_write_atomic(fa, "a\n", 2, 0644));
    assert(tam_fs_write_atomic(fb, "b\n", 2, 0644));

    knox_explorer_t ex;
    assert(knox_explorer_init(&ex, root, false) == 0);
    assert(!flat_contains_name(&ex, "alpha.txt"));
    assert(!flat_contains_name(&ex, "beta.txt"));

    /* Reveal direct child repo */
    size_t idx = 0;
    assert(knox_explorer_reveal_path(&ex, repo_a, NULL, &idx) == 0);
    assert(idx < ex.flat_count);
    assert(strcmp(ex.flat[idx]->name, "repo-a") == 0);
    assert(ex.flat[idx]->expanded);
    assert(flat_contains_name(&ex, "alpha.txt"));

    /* Active filter is cleared when revealing a nested repo */
    assert(knox_explorer_set_filter(&ex, "alpha", NULL) == 0);
    assert(ex.filter[0] != '\0');
    assert(knox_explorer_reveal_path(&ex, repo_b, NULL, &idx) == 0);
    assert(ex.filter[0] == '\0');
    assert(strcmp(ex.flat[idx]->name, "repo-b") == 0);
    assert(ex.flat[idx]->expanded);
    assert(flat_contains_name(&ex, "beta.txt"));

    /* Reveal root itself (single-repo launch) */
    assert(knox_explorer_reveal_path(&ex, root, NULL, &idx) == 0);
    assert(idx == 0);
    assert(ex.flat[0] == ex.root);

    /* Non-descendant path returns -1 */
    assert(knox_explorer_reveal_path(&ex, "/tmp", NULL, &idx) == -1);

    knox_explorer_free(&ex);
    free(fb);
    free(fa);
    free(repo_b);
    free(nested_parent);
    free(repo_a);
    tam_fs_remove_tree(-100, root);
}

static size_t g_progress_calls = 0;

static void batch_progress_cb(size_t idx, size_t total,
                              const knox_repo_t *repo, void *user)
{
    (void)idx;
    (void)user;
    assert(total == 3);
    assert(repo != NULL && repo->name != NULL);
    g_progress_calls++;
}

static void test_batch_fetch(void)
{
    char tpl[] = "/tmp/knox-fetch-XXXXXX";
    char *ws_dir = mkdtemp(tpl);
    assert(ws_dir != NULL);

    char *remote_dir = tam_path_join(ws_dir, "remote.git");
    char *repo_a = tam_path_join(ws_dir, "repo-a");
    char *repo_b = tam_path_join(ws_dir, "repo-b");
    char *repo_local = tam_path_join(ws_dir, "repo-local");
    assert(tam_fs_mkdir_p(remote_dir, 0755));
    assert(tam_fs_mkdir_p(repo_a, 0755));
    assert(tam_fs_mkdir_p(repo_local, 0755));

    run_cmd((const char *[]){"git", "-C", remote_dir, "init", "--bare", "-b", "dev", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "init", "-b", "dev", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "config", "user.name", "Test", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "config", "user.email", "t@e.com", NULL});

    char *fa = tam_path_join(repo_a, "a.txt");
    assert(tam_fs_write_atomic(fa, "v1\n", 3, 0644));
    run_cmd((const char *[]){"git", "-C", repo_a, "add", "--", "a.txt", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "commit", "-m", "init", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "remote", "add", "origin", remote_dir, NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "push", "-u", "origin", "dev", NULL});

    run_cmd((const char *[]){"git", "clone", remote_dir, repo_b, NULL});

    /* Push a second commit from repo-a so repo-b falls behind */
    assert(tam_fs_write_atomic(fa, "v2\n", 3, 0644));
    run_cmd((const char *[]){"git", "-C", repo_a, "add", "--", "a.txt", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "commit", "-m", "v2", NULL});
    run_cmd((const char *[]){"git", "-C", repo_a, "push", NULL});

    /* Local-only repo without any remote */
    run_cmd((const char *[]){"git", "-C", repo_local, "init", "-b", "dev", NULL});
    run_cmd((const char *[]){"git", "-C", repo_local, "config", "user.name", "Test", NULL});
    run_cmd((const char *[]){"git", "-C", repo_local, "config", "user.email", "t@e.com", NULL});
    char *fl = tam_path_join(repo_local, "local.txt");
    assert(tam_fs_write_atomic(fl, "only local\n", 11, 0644));
    run_cmd((const char *[]){"git", "-C", repo_local, "add", "--", "local.txt", NULL});
    run_cmd((const char *[]){"git", "-C", repo_local, "commit", "-m", "local init", NULL});

    knox_workspace_t ws;
    knox_workspace_init(&ws, ws_dir);
    assert(knox_discover_repos(&ws, 2) == 0);
    assert(ws.count == 3);

    g_progress_calls = 0;
    knox_batch_fetch_result_t bres = {0};
    char summary[256] = "";
    assert(knox_workspace_fetch_all(&ws, batch_progress_cb, NULL,
                                    &bres, summary, sizeof(summary)) == 0);
    assert(g_progress_calls == 3);
    assert(bres.total == 3);
    assert(bres.fetched == 2);
    assert(bres.skipped == 1);
    assert(bres.failed == 0);
    assert(bres.behind_any == 1);
    assert(strstr(summary, "Fetched 2/3") != NULL);
    assert(strstr(summary, "1 behind upstream") != NULL);

    /* Verify repo-b is behind by 1 and working tree was NOT pulled/modified */
    knox_repo_t *rb = NULL;
    for (size_t i = 0; i < ws.count; i++) {
        if (strcmp(ws.repos[i].name, "repo-b") == 0)
            rb = &ws.repos[i];
    }
    assert(rb != NULL);
    assert(rb->behind == 1);
    assert(rb->ahead == 0);

    knox_workspace_free(&ws);
    free(fl);
    free(fa);
    free(repo_local);
    free(repo_b);
    free(repo_a);
    free(remote_dir);
    tam_fs_remove_tree(-100, ws_dir);
}

static void test_keymap_ini(void)
{
    tam_key_type_t type = TAM_KEY_NONE;
    uint32_t cp = 0;
    unsigned mods = 0;

    /* Valid physical key specs */
    assert(knox_keymap_parse_key("Alt+A", &type, &cp, &mods));
    assert(type == TAM_KEY_CHAR && cp == 'a' && mods == TAM_MOD_ALT);

    char fmt[32];
    knox_keymap_format_key(type, cp, mods, fmt, sizeof(fmt));
    assert(strcmp(fmt, "Alt+A") == 0);

    assert(knox_keymap_parse_key("Ctrl+F", &type, &cp, &mods));
    assert(type == TAM_KEY_CHAR && cp == 'f' && mods == TAM_MOD_CTRL);

    assert(knox_keymap_parse_key("F5", &type, &cp, &mods));
    assert(type == TAM_KEY_F5 && mods == 0);

    assert(knox_keymap_parse_key("Shift+Tab", &type, &cp, &mods));
    assert(type == TAM_KEY_TAB && mods == TAM_MOD_SHIFT);

    /* Meta must be rejected per Tamlinux keybinding standard */
    assert(!knox_keymap_parse_key("Meta+K", &type, &cp, &mods));
    assert(!knox_keymap_parse_key("Meta+Shift+Q", &type, &cp, &mods));

    /* Load INI string with valid overrides, unbind, and one invalid entry */
    const char *ini =
        "# User keymap\n"
        "[keymap]\n"
        "F5 = update\n"
        "Alt+X = fetch-all\n"
        "q = unbind\n"
        "Meta+Q = quit\n"
        "F6 = nonexistent-widget\n";

    knox_keymap_t km;
    knox_keymap_init(&km);
    assert(knox_keymap_load_string(&km, ini) == 0);
    assert(km.count == 3);
    assert(km.warning_count == 2); /* Meta+Q + nonexistent-widget */

    tam_key_event_t ev_f5 = { .type = TAM_KEY_F5, .codepoint = 0, .mods = 0 };
    const char *w = knox_keymap_lookup(&km, &ev_f5);
    assert(w != NULL && strcmp(w, "update") == 0);
    assert(knox_keymap_is_valid_widget("repo-fetch"));
    assert(knox_keymap_is_valid_widget("explore-repo"));
    assert(knox_keymap_is_valid_widget("tab-1"));
    assert(knox_keymap_is_valid_widget("tab-5"));
    assert(knox_keymap_is_valid_widget("tab-9"));
    assert(knox_keymap_is_valid_widget("tab-0"));
    assert(!knox_keymap_is_valid_widget("tab-10"));

    tam_key_event_t ev_ax = { .type = TAM_KEY_CHAR, .codepoint = 'x', .mods = TAM_MOD_ALT };
    w = knox_keymap_lookup(&km, &ev_ax);
    assert(w != NULL && strcmp(w, "fetch-all") == 0);

    tam_key_event_t ev_q = { .type = TAM_KEY_CHAR, .codepoint = 'q', .mods = 0 };
    w = knox_keymap_lookup(&km, &ev_q);
    assert(w != NULL && strcmp(w, "unbind") == 0);

    char found_key[32] = "";
    assert(knox_keymap_find_widget_key(&km, "fetch-all", found_key, sizeof(found_key)));
    assert(strcmp(found_key, "Alt+X") == 0);

    knox_keymap_free(&km);
}

int main(void)
{
    test_explorer_filter();
    test_explorer_reveal();
    test_batch_fetch();
    test_keymap_ini();
    printf("test-p3: all explorer filter, reveal, batch fetch, and keymap.ini tests passed\n");
    return 0;
}
