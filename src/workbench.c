#include "knox.h"
#include <tam_render.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TABS 16

typedef enum {
    SIDEBAR_SOURCE_CONTROL = 0,
    SIDEBAR_EXPLORER
} sidebar_mode_t;

typedef enum {
    FOCUS_SIDEBAR_REPOS = 0,
    FOCUS_SIDEBAR_CHANGES,
    FOCUS_SIDEBAR_EXPLORER,
    FOCUS_MAIN_PANE
} focus_zone_t;

typedef enum {
    MODAL_NONE = 0,
    MODAL_GUIDANCE,
    MODAL_PALETTE,
    MODAL_SEARCH,
    MODAL_COMMIT,
    MODAL_CONFIRM_SYNC,
    MODAL_BRANCH,
    MODAL_BRANCH_CREATE,
    MODAL_STASH,
    MODAL_STASH_PUSH,
    MODAL_CONFIRM_STASH_DROP
} modal_kind_t;

typedef struct {
    const char *name;
    const char *keys;
    const char *desc;
} knox_widget_cmd_t;

static const knox_widget_cmd_t WIDGET_TABLE[] = {
    { "view-source-control", "Alt+1 / Bksp",  "Switch left sidebar to Source Control (Repos + Changes)" },
    { "view-explorer",       "Alt+2",         "Switch left sidebar to Folder Explorer tree" },
    { "explore-repo",        "Enter / Right", "Open and expand active repository in Folder Explorer (Repos)" },
    { "view-graph",          "Alt+3",         "Focus or open the Git Graph tab" },
    { "toggle-sidebar",      "Alt+B",         "Show or hide the left sidebar" },
    { "focus-next",          "Tab",           "Cycle focus across Sidebar and Main Workbench panes" },
    { "focus-prev",          "Shift+Tab",     "Cycle focus backward" },
    { "open-or-inspect",     "Enter",         "Explore repo, expand folder, open file, or inspect commit" },
    { "stage-or-page-down",  "Space",         "Stage/unstage file in Changes, or page down in viewer" },
    { "move-up",             "Up / k",        "Move selection or cursor up one item or line" },
    { "move-down",           "Down / j",      "Move selection or cursor down one item or line" },
    { "page-up",             "PageUp / b",    "Scroll or move selection up one page (12 lines)" },
    { "page-down",           "PageDown",      "Scroll or move selection down one page (12 lines)" },
    { "jump-top",            "Home / Alt+<",  "Jump to first item or top of document" },
    { "jump-bottom",         "End / Alt+>",   "Jump to last item or bottom of document" },
    { "scroll-left",         "Left",          "Collapse folder, return to Explorer, or scroll Viewer left" },
    { "scroll-right",        "Right",         "Explore repo, expand/open in Explorer, or scroll Viewer right" },
    { "repo-fetch",          "f",             "Fetch active repository only from remote (git fetch --prune)" },
    { "fetch-all",           "F / Alt+A",     "Fetch all repositories with remotes (git fetch --prune)" },
    { "repo-sync",           "s / S / Alt+S", "Sync active repository with remote (fetch, pull --ff-only, push)" },
    { "commit-prompt",       "Alt+C / Ctrl+S","Commit staged changes in active repository" },
    { "branch-modal",        "c / C / Alt+K", "Checkout or create a branch in active repository" },
    { "stash-modal",         "z / Z / Alt+Z", "Manage stashes (list, push, inspect diff, apply, pop, drop)" },
    { "toggle-diff-split",   "d / D / Alt+D", "Toggle Side-by-Side Split Diff vs. Unified Diff mode" },
    { "open-in-editor",      "Alt+E",         "Open current file at current line in tam-edit" },
    { "toggle-render-mode",  "r / R / Alt+R", "Toggle Rendered (MD/HTML/Mermaid) vs. Raw Syntax view" },
    { "cycle-branch-filter", "B / Alt+F",     "Toggle Git Graph between All Branches and HEAD" },
    { "split-vertical",      "Alt+V",         "Split active main pane side-by-side (left / right)" },
    { "split-horizontal",    "Alt+H",         "Split active main pane stacked (top / bottom)" },
    { "focus-other-pane",    "Alt+W",         "Cycle focus among split panes in the active tab" },
    { "tab-prev",            "Alt+[",         "Switch to previous workbench tab" },
    { "tab-next",            "Alt+]",         "Switch to next workbench tab (1-0 selects)" },
    { "new",                 "Ctrl+N",        "Open a new workbench tab" },
    { "close",               "Ctrl+D",        "Close active split pane or workbench tab" },
    { "find",                "Ctrl+F / /",    "Find in repo list, Explorer tree, commit graph, or file" },
    { "find-next",           "n",             "Jump to next search match" },
    { "find-prev",           "N",             "Jump to previous search match" },
    { "update",              "u / Ctrl+U",    "Refresh Git status, graph, Explorer, and open files from disk" },
    { "copy",                "Ctrl+C",        "Copy selection, commit hash, or file path (OSC 52)" },
    { "execute",             "Ctrl+E",        "Open Command Palette & One-Handed Chord Composer" },
    { "guidance",            "g / G / Ctrl+G","Show keybinding guidance overlay" },
    { "cancel-mode",         "Esc / Ctrl+\\", "Close modal/prompt, collapse commit details, or clear filter" },
    { "clear-screen",        "Ctrl+L",        "Clear and redraw terminal screen" },
    { "quit",                "q / Q / Ctrl+Q","Quit tam-knox" }
};

#define WIDGET_COUNT (sizeof(WIDGET_TABLE) / sizeof(WIDGET_TABLE[0]))

static const knox_widget_cmd_t GUIDANCE_EXTRA_TABLE[] = {
    { "tab-1..tab-0", "(tabs)",         "1-9, 0: Select workbench tab 1-10 directly (0 selects tab 10)" },
    { "(commit file)","v",              "In Commit Detail inspector, view full file snapshot at commit in a new tab" },
    { "(Branch modal)","Enter / Alt+N", "Enter: checkout branch, Alt+N: create branch, type to filter, Esc: close" },
    { "(Stash modal)", "n/a/p/d/Enter", "n/s: push, Enter: diff, a: apply, p: pop, d/x: drop, j/k: move, q/Esc: close" },
    { "(Guidance)",    "Up/Dn/PgUp/Dn", "Scroll guidance (j/k, b/Space, Home/End); g/q/Esc close (* = keymap.ini override)" }
};

#define GUIDANCE_EXTRA_COUNT (sizeof(GUIDANCE_EXTRA_TABLE) / sizeof(GUIDANCE_EXTRA_TABLE[0]))

typedef struct {
    knox_options_t      opts;
    tam_tty_t           tty;
    tam_key_parser_t    keys;
    knox_keymap_t       keymap;
    knox_workspace_t    ws;
    knox_explorer_t     explorer;
    /* Per-repo cached graph and active commit details */
    knox_graph_t        graph;
    size_t              graph_repo_idx;
    bool                graph_loaded;
    knox_commit_details_t details;
    char                details_hash[41];
    size_t              details_repo_idx;
    /* Sidebar state */
    bool                sidebar_visible;
    int                 sidebar_width;
    sidebar_mode_t      sidebar_mode;
    focus_zone_t        focus;
    size_t              active_repo_idx;
    size_t              repo_top;
    size_t              change_cursor;
    size_t              change_top;
    size_t              explorer_cursor;
    size_t              explorer_top;
    /* Tabs & Split Panes */
    knox_tab_t          tabs[MAX_TABS];
    size_t              tab_count;
    size_t              active_tab;
    /* Modal & Prompt state */
    modal_kind_t        modal;
    char                prompt_buf[256];
    size_t              prompt_len;
    size_t              palette_cursor;
    size_t              guidance_top;
    bool                guidance_sort_by_command;
    unsigned            chord_mods; /* Latched modifiers in Ctrl+E composer */
    /* Branch & Stash modal lists */
    knox_branch_info_t *branch_list;
    size_t              branch_count;
    size_t              branch_cursor;
    size_t              branch_top;
    knox_stash_entry_t *stash_list;
    size_t              stash_count;
    size_t              stash_cursor;
    size_t              stash_top;
    char                pending_stash_ref[64];
    /* Status bar & clipboard */
    char                status_msg[256];
    char                clipboard[512];
    bool                running;
} knox_app_t;

static volatile sig_atomic_t g_winch_flag = 0;

static void sigwinch_handler(int sig)
{
    (void)sig;
    g_winch_flag = 1;
}

/* ========================================================================= */
/* Screen Line Buffer & UTF-8 Cell Clipping                                  */
/* ========================================================================= */

/* Write a string into `out` padded or clipped to exactly `target_cols`
 * visible terminal cells, preserving embedded ANSI SGR sequences. */
static size_t format_clipped_padded(char *out, size_t cap, const char *src,
                                    int target_cols, bool pad_right)
{
    if (!out || cap == 0 || target_cols <= 0) {
        if (out && cap > 0)
            out[0] = '\0';
        return 0;
    }

    size_t pos = 0;
    int vis = 0;
    const char *p = src ? src : "";
    bool saw_esc = false;

    while (*p && vis < target_cols) {
        if (*p == '\033' && p[1] == '[') {
            saw_esc = true;
            const char *start = p;
            p += 2;
            while (*p && !((*p >= '@' && *p <= '~')))
                p++;
            if (*p)
                p++;
            size_t elen = (size_t)(p - start);
            if (pos + elen + 8 < cap) {
                memcpy(out + pos, start, elen);
                pos += elen;
            }
            continue;
        }
        uint32_t cp = 0;
        size_t clen = tam_utf8_decode(p, strlen(p), &cp);
        if (clen == 0)
            break;
        if (cp < 0x20) {
            p += clen;
            continue;
        }
        int cw = tam_uni_width(cp);
        if (cw < 0)
            cw = 1;
        if (vis + cw > target_cols)
            break;
        if (pos + clen + 8 < cap) {
            memcpy(out + pos, p, clen);
            pos += clen;
            vis += cw;
        }
        p += clen;
    }

    if (saw_esc && pos + 5 < cap) {
        memcpy(out + pos, "\033[0m", 4);
        pos += 4;
    }

    if (pad_right) {
        while (vis < target_cols && pos + 2 < cap) {
            out[pos++] = ' ';
            vis++;
        }
    }
    out[pos] = '\0';
    return pos;
}

static void frame_move_write(tam_str_t *frame, int row_1based, int col_1based,
                             const char *text, int width, const char *bg_sgr,
                             bool pad)
{
    char clipped[4096];
    format_clipped_padded(clipped, sizeof(clipped), text, width, pad);
    *frame = tam_str_cat_printf(*frame, "\033[%d;%dH%s%s%s",
                                row_1based, col_1based,
                                bg_sgr ? bg_sgr : "",
                                clipped,
                                (bg_sgr && *bg_sgr) ? "\033[0m" : "");
}

/* ========================================================================= */
/* App Helpers: Graph Loading, Commit Details, Tabs, Clipboard               */
/* ========================================================================= */

static void app_set_status(knox_app_t *app, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->status_msg, sizeof(app->status_msg), fmt, ap);
    va_end(ap);
}

static void app_copy_to_clipboard(knox_app_t *app, const char *text)
{
    if (!text || !*text)
        return;
    snprintf(app->clipboard, sizeof(app->clipboard), "%s", text);

    /* OSC 52 write when not on TERM=linux */
    if (!app->opts.linux_vt && app->tty.interactive &&
        tam_tty_osc52_copy(&app->tty, text, strlen(text)) < 0 && errno == EMSGSIZE) {
        app_set_status(app, "Too large for the terminal clipboard; kept here only");
        return;
    }
    app_set_status(app, "Copied to clipboard: %s", text);
}

static void app_ensure_repo_graph(knox_app_t *app, size_t repo_idx,
                                  bool all_branches, bool show_remotes,
                                  bool force_reload)
{
    if (app->ws.count == 0 || repo_idx >= app->ws.count)
        return;
    if (!force_reload && app->graph_loaded && app->graph_repo_idx == repo_idx)
        return;

    knox_repo_load_graph(&app->ws.repos[repo_idx], &app->graph, 250,
                         all_branches, show_remotes, app->opts.ascii);
    app->graph_repo_idx = repo_idx;
    app->graph_loaded = true;
}

static void app_ensure_commit_details(knox_app_t *app, size_t repo_idx,
                                      size_t commit_idx)
{
    if (repo_idx >= app->ws.count)
        return;
    app_ensure_repo_graph(app, repo_idx, true, true, false);
    if (commit_idx >= app->graph.commit_count)
        return;
    const char *hash = app->graph.commits[commit_idx].hash;
    if (app->details_repo_idx == repo_idx &&
        strcmp(app->details_hash, hash) == 0 &&
        app->details.author != NULL)
        return;

    knox_commit_details_free(&app->details);
    knox_repo_load_commit_details(&app->ws.repos[repo_idx], hash, &app->details);
    snprintf(app->details_hash, sizeof(app->details_hash), "%s", hash);
    app->details_repo_idx = repo_idx;
}

static knox_tab_t *app_current_tab(knox_app_t *app)
{
    if (app->tab_count == 0)
        return NULL;
    if (app->active_tab >= app->tab_count)
        app->active_tab = app->tab_count - 1;
    return &app->tabs[app->active_tab];
}

static knox_pane_t *app_active_pane(knox_app_t *app)
{
    knox_tab_t *tab = app_current_tab(app);
    return tab ? tab->active_pane : NULL;
}

static knox_tab_t *app_add_tab(knox_app_t *app, const char *title,
                               knox_pane_t *initial_pane)
{
    if (app->tab_count >= MAX_TABS) {
        /* Reuse last tab if full */
        knox_tab_t *last = &app->tabs[MAX_TABS - 1];
        knox_split_free_tree(last->root);
        snprintf(last->title, sizeof(last->title), "%s", title);
        last->root = knox_split_create_leaf(initial_pane);
        last->active_pane = initial_pane;
        app->active_tab = MAX_TABS - 1;
        return last;
    }
    knox_tab_t *t = &app->tabs[app->tab_count];
    snprintf(t->title, sizeof(t->title), "%s", title);
    t->root = knox_split_create_leaf(initial_pane);
    t->active_pane = initial_pane;
    app->active_tab = app->tab_count++;
    return t;
}

static void app_open_file_in_tab(knox_app_t *app, const char *abs_path)
{
    if (!abs_path)
        return;
    knox_repo_t *repo = knox_workspace_find_repo_for_path(&app->ws, abs_path);
    const char *repo_name = repo ? repo->name : NULL;

    /* Check if file is already open in a tab */
    for (size_t i = 0; i < app->tab_count; i++) {
        knox_pane_t *p = app->tabs[i].active_pane;
        if (p && p->content == KNOX_PANE_VIEWER &&
            p->doc.abs_path && strcmp(p->doc.abs_path, abs_path) == 0) {
            app->active_tab = i;
            app->focus = FOCUS_MAIN_PANE;
            return;
        }
    }

    /* Check dirty badge for tab title */
    char badge = ' ';
    if (repo) {
        size_t rlen = strlen(repo->root_path);
        if (strncmp(abs_path, repo->root_path, rlen) == 0 &&
            abs_path[rlen] == '/') {
            const char *rel = abs_path + rlen + 1;
            for (size_t c = 0; c < repo->change_count; c++) {
                if (strcmp(repo->changes[c].path, rel) == 0) {
                    badge = (char)((repo->changes[c].worktree_status != KNOX_CHANGE_NONE)
                                   ? repo->changes[c].worktree_status
                                   : repo->changes[c].index_status);
                    break;
                }
            }
        }
    }

    const char *base = strrchr(abs_path, '/');
    base = (base && base[1]) ? base + 1 : abs_path;
    char tab_title[64];
    if (repo_name && badge != ' ')
        snprintf(tab_title, sizeof(tab_title), "%s %s %c", base, repo_name, badge);
    else if (repo_name)
        snprintf(tab_title, sizeof(tab_title), "%s %s", base, repo_name);
    else
        snprintf(tab_title, sizeof(tab_title), "%s", base);

    knox_pane_t *pane = knox_pane_new(KNOX_PANE_VIEWER, app->active_repo_idx);
    if (!pane)
        return;
    int render_w = app->tty.cols - (app->sidebar_visible ? app->sidebar_width + 1 : 0) - 4;
    if (render_w < 40)
        render_w = 72;
    if (knox_viewer_open_file(&pane->doc, abs_path, repo_name, true,
                              render_w, app->opts.ascii) != 0) {
        knox_pane_free(pane);
        app_set_status(app, "Unable to read file: %s", abs_path);
        return;
    }

    app_add_tab(app, tab_title, pane);
    app->focus = FOCUS_MAIN_PANE;
    app_set_status(app, "Opened %s (%s)", tab_title,
                   pane->doc.render_mode ? "Rendered" :
                   knox_syntax_lang_name(pane->doc.lang));
}

static void app_open_diff_in_pane_or_tab(knox_app_t *app, const char *title,
                                         const char *repo_name,
                                         const char *breadcrumb,
                                         char *diff_text)
{
    if (!diff_text)
        return;

    /* If active tab has a viewer split pane already, reuse it; otherwise open tab */
    knox_tab_t *tab = app_current_tab(app);
    if (tab && tab->active_pane && tab->active_pane->content == KNOX_PANE_VIEWER &&
        tab->active_pane->doc.kind == KNOX_DOC_DIFF) {
        snprintf(tab->title, sizeof(tab->title), "%s", title);
        knox_viewer_open_buffer(&tab->active_pane->doc, KNOX_DOC_DIFF,
                                title, repo_name, breadcrumb,
                                diff_text, strlen(diff_text), KNOX_LANG_DIFF);
        free(diff_text);
        app->focus = FOCUS_MAIN_PANE;
        return;
    }

    knox_pane_t *pane = knox_pane_new(KNOX_PANE_VIEWER, app->active_repo_idx);
    if (!pane) {
        free(diff_text);
        return;
    }
    knox_viewer_open_buffer(&pane->doc, KNOX_DOC_DIFF,
                            title, repo_name, breadcrumb,
                            diff_text, strlen(diff_text), KNOX_LANG_DIFF);
    free(diff_text);
    app_add_tab(app, title, pane);
    app->focus = FOCUS_MAIN_PANE;
}

static void app_inspect_commit_in_split(knox_app_t *app, knox_pane_t *graph_pane)
{
    knox_tab_t *tab = app_current_tab(app);
    if (!tab || !graph_pane)
        return;

    app_ensure_commit_details(app, graph_pane->repo_idx, graph_pane->graph_cursor);

    /* Find if a KNOX_PANE_COMMIT_DETAIL pane already exists in this tab */
    knox_pane_t *cur = graph_pane;
    size_t nleaves = knox_split_count_leaves(tab->root);
    for (size_t i = 0; i < nleaves; i++) {
        cur = knox_split_next_pane(tab->root, cur);
        if (cur && cur->content == KNOX_PANE_COMMIT_DETAIL) {
            cur->repo_idx = graph_pane->repo_idx;
            cur->inspect_commit_idx = graph_pane->graph_cursor;
            cur->inspect_file_cursor = 0;
            cur->inspect_file_top = 0;
            tab->active_pane = cur;
            app->focus = FOCUS_MAIN_PANE;
            return;
        }
    }

    /* Otherwise split the graph pane horizontally (top = graph, bottom = details) */
    knox_split_node_t *leaf = knox_split_find_pane(tab->root, graph_pane);
    if (!leaf)
        return;
    knox_pane_t *detail_pane = knox_pane_new(KNOX_PANE_COMMIT_DETAIL,
                                             graph_pane->repo_idx);
    if (!detail_pane)
        return;
    detail_pane->inspect_commit_idx = graph_pane->graph_cursor;
    if (knox_split_pane(leaf, KNOX_NODE_SPLIT_H, detail_pane)) {
        leaf->ratio = 0.55f;
        tab->active_pane = detail_pane;
        app->focus = FOCUS_MAIN_PANE;
    } else {
        knox_pane_free(detail_pane);
    }
}

static bool app_reload_file_pane(knox_app_t *app, knox_pane_t *pane)
{
    knox_viewer_doc_t *doc = &pane->doc;
    knox_viewer_doc_t fresh = {0};
    int width = pane->w > 4 ? pane->w - 2 : 72;
    if (knox_viewer_open_file(&fresh, doc->abs_path, doc->repo_name, false,
                              width, app->opts.ascii) != 0 || !fresh.abs_path) {
        knox_viewer_free(&fresh);
        pane->file_stale = true;
        return false;
    }

    fresh.markup_fmt = doc->markup_fmt;
    fresh.render_mode = doc->render_mode &&
        knox_viewer_ensure_rendered(&fresh, width, app->opts.ascii) == 0;
    size_t lines = fresh.render_mode ? fresh.rend_line_count : fresh.line_count;
    size_t last_line = lines > 0 ? lines - 1 : 0;
    fresh.cursor_line = doc->cursor_line < last_line ? doc->cursor_line : last_line;
    fresh.top_line = doc->top_line < fresh.cursor_line ? doc->top_line : fresh.cursor_line;
    fresh.left_col = doc->left_col;
    fresh.wrap_lines = doc->wrap_lines;
    memcpy(fresh.search_query, doc->search_query, sizeof(fresh.search_query));

    knox_viewer_free(doc);
    *doc = fresh;
    pane->file_stale = false;
    return true;
}

static void app_reload_file_panes(knox_app_t *app, knox_split_node_t *node,
                                  size_t *reloaded, size_t *stale)
{
    if (!node)
        return;
    if (node->type != KNOX_NODE_LEAF) {
        app_reload_file_panes(app, node->child1, reloaded, stale);
        app_reload_file_panes(app, node->child2, reloaded, stale);
        return;
    }
    knox_pane_t *pane = node->pane;
    if (pane && pane->content == KNOX_PANE_VIEWER && pane->doc.abs_path) {
        if (app_reload_file_pane(app, pane))
            (*reloaded)++;
        else
            (*stale)++;
    }
}

static void app_update(knox_app_t *app)
{
    for (size_t i = 0; i < app->ws.count; i++)
        knox_repo_refresh_status(&app->ws.repos[i]);
    knox_explorer_rebuild(&app->explorer, &app->ws);
    app->graph_loaded = false;

    size_t reloaded = 0;
    size_t stale = 0;
    for (size_t i = 0; i < app->tab_count; i++)
        app_reload_file_panes(app, app->tabs[i].root, &reloaded, &stale);

    if (stale)
        app_set_status(app, "Reloaded %zu file views, %zu stale (could not read from disk)",
                       reloaded, stale);
    else
        app_set_status(app, "Refreshed %zu repositories and %zu file views",
                       app->ws.count, reloaded);
}

static void app_open_in_external_editor(knox_app_t *app)
{
    const char *path = NULL;
    size_t line_1based = 1;

    knox_pane_t *ap = app_active_pane(app);
    if (app->focus == FOCUS_MAIN_PANE && ap &&
        ap->content == KNOX_PANE_VIEWER && ap->doc.abs_path) {
        path = ap->doc.abs_path;
        line_1based = ap->doc.cursor_line + 1;
    } else if (app->focus == FOCUS_SIDEBAR_EXPLORER &&
               app->explorer_cursor < app->explorer.flat_count) {
        knox_tree_node_t *n = app->explorer.flat[app->explorer_cursor];
        if (!n->is_dir)
            path = n->abs_path;
    } else if (app->focus == FOCUS_SIDEBAR_CHANGES &&
               app->active_repo_idx < app->ws.count) {
        knox_repo_t *r = &app->ws.repos[app->active_repo_idx];
        if (app->change_cursor < r->change_count) {
            static char joined_buf[4096];
            snprintf(joined_buf, sizeof(joined_buf), "%s/%s",
                     r->root_path, r->changes[app->change_cursor].path);
            path = joined_buf;
        }
    }

    if (!path) {
        app_set_status(app, "Select a file in Viewer, Explorer, or Changes to open in tam-edit");
        return;
    }

    const char *editor = getenv("VISUAL");
    if (!editor || !*editor)
        editor = getenv("EDITOR");
    if (!editor || !*editor)
        editor = "tam-edit";

    char line_arg[32];
    snprintf(line_arg, sizeof(line_arg), "+%zu", line_1based);

    /* Leave the terminal as the editor expects it; tam-knox ignores
     * Ctrl+C until the editor ends, and comes back with the modes it had. */
    tam_tty_suspend(&app->tty);
    const char *with_line[] = { editor, line_arg, path, NULL };
    if (tam_run_foreground(with_line, NULL) < 0 && errno == ENOENT) {
        const char *fallback[] = { "tam-edit", path, NULL };
        (void)tam_run_foreground(fallback, NULL);
    }
    tam_tty_resume(&app->tty);

    /* Refresh repo status and open file buffer after edit */
    if (app->active_repo_idx < app->ws.count)
        knox_repo_refresh_status(&app->ws.repos[app->active_repo_idx]);
    knox_explorer_refresh_git_status(&app->explorer, &app->ws);
    if (ap && ap->content == KNOX_PANE_VIEWER && ap->doc.abs_path &&
        strcmp(ap->doc.abs_path, path) == 0)
        app_reload_file_pane(app, ap);
    app_set_status(app, "Returned from %s", editor);
}

/* ========================================================================= */
/* Rendering: Top Bar, Sidebar, Panes, Modals, and Footer                    */
/* ========================================================================= */

/* High-contrast SGR helpers tailored for both 256-color tam-last and 16-color
 * TERM=linux consoles. In tam-last, ANSI 4 (blue) is a light pastel periwinkle
 * and ANSI 8 (90m) is a very dark slate, so we use explicit 256-color pure
 * white (231) on deep navy (24) / charcoal (237) for selections and headers,
 * and bright silver (250/247) for secondary metadata. */
static const char *sgr_header_focused(const knox_app_t *app)
{
    if (!app->opts.color)
        return "\033[7m";
    return app->opts.linux_vt ? "\033[1;30;46m" : "\033[1;38;5;16;46m";
}

static const char *sgr_header_unfocused(const knox_app_t *app)
{
    if (!app->opts.color)
        return "";
    return app->opts.linux_vt ? "\033[1;37;40m" : "\033[1;38;5;231;48;5;237m";
}

static const char *sgr_row_selected(const knox_app_t *app, bool zone_focused)
{
    if (!app->opts.color)
        return "\033[7m";
    if (app->opts.linux_vt)
        return zone_focused ? "\033[1;37;44m" : "\033[7m";
    return zone_focused
           ? "\033[1;38;5;231;48;5;24m"   /* Bold pure white #ffffff on deep blue #005f87 */
           : "\033[1;38;5;231;48;5;238m"; /* Bold pure white #ffffff on dark slate #444444 */
}

static const char *sgr_muted(const knox_app_t *app)
{
    if (!app->opts.color)
        return "";
    return app->opts.linux_vt ? "\033[37m" : "\033[38;5;248m";
}

static int visible_text_cols(const char *s)
{
    if (!s)
        return 0;
    int vis = 0;
    const char *p = s;
    while (*p) {
        uint32_t cp = 0;
        size_t clen = tam_utf8_decode(p, strlen(p), &cp);
        if (clen == 0)
            break;
        if (cp >= 0x20) {
            int cw = tam_uni_width(cp);
            vis += (cw < 0) ? 1 : cw;
        }
        p += clen;
    }
    return vis;
}

static void truncate_text_cols(const char *src, int max_cols, bool ascii,
                               char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    if (!src || max_cols <= 0) {
        out[0] = '\0';
        return;
    }
    if (visible_text_cols(src) <= max_cols) {
        snprintf(out, cap, "%s", src);
        return;
    }
    const char *ell = ascii ? "~" : "\xe2\x80\xa6";
    size_t ell_len = strlen(ell);
    int limit = (max_cols >= 2) ? max_cols - 1 : max_cols;
    size_t pos = 0;
    int vis = 0;
    const char *p = src;
    while (*p && vis < limit) {
        uint32_t cp = 0;
        size_t clen = tam_utf8_decode(p, strlen(p), &cp);
        if (clen == 0)
            break;
        if (cp < 0x20) {
            p += clen;
            continue;
        }
        int cw = tam_uni_width(cp);
        if (cw < 0)
            cw = 1;
        if (vis + cw > limit)
            break;
        if (pos + clen + ell_len + 1 >= cap)
            break;
        memcpy(out + pos, p, clen);
        pos += clen;
        vis += cw;
        p += clen;
    }
    if (max_cols >= 2 && pos + ell_len < cap) {
        memcpy(out + pos, ell, ell_len);
        pos += ell_len;
    }
    out[pos] = '\0';
}

static void render_top_bar(knox_app_t *app, tam_str_t *frame, int cols)
{
    const char *ws_base = strrchr(app->ws.root_path, '/');
    ws_base = (ws_base && ws_base[1]) ? ws_base + 1 : app->ws.root_path;

    const char *bar_bg = (app->opts.color && !app->opts.linux_vt)
                         ? "\033[38;5;254;48;5;236m" : "\033[7m";
    const struct tam_render_box *bx = tam_render_box_chars(app->opts.ascii);

    char prefix_plain[256];
    snprintf(prefix_plain, sizeof(prefix_plain),
             " tam-knox %s (%zu repos) ", ws_base, app->ws.count);

    tam_str_t prefix = tam_str_new("");
    if (app->opts.color) {
        prefix = tam_str_cat_printf(prefix,
            " \033[1;36;48;5;236mtam-knox\033[0m%s \033[1;38;5;231;48;5;236m%s\033[0m%s (%zu repos) ",
            bar_bg, ws_base, bar_bg, app->ws.count);
    } else {
        prefix = tam_str_cat(prefix, prefix_plain);
    }

    int avail = 0;
    if (app->sidebar_visible) {
        avail = cols - app->sidebar_width - 1;
    } else {
        avail = cols - visible_text_cols(prefix_plain);
    }
    if (avail < 0)
        avail = 0;

    size_t ntabs = app->tab_count;
    if (ntabs > MAX_TABS)
        ntabs = MAX_TABS;

    int full_w[MAX_TABS];
    int alloc_w[MAX_TABS];
    int fixed_sum = 0;
    int full_sum = 0;
    for (size_t i = 0; i < ntabs; i++) {
        int num_len = (i <= 9) ? 1 : 2;
        int lead_sep = (app->sidebar_visible && i == 0) ? 0 : 1;
        fixed_sum += lead_sep + 3 + num_len;
        full_w[i] = visible_text_cols(app->tabs[i].title);
        alloc_w[i] = full_w[i];
        full_sum += full_w[i];
    }

    if (ntabs > 0 && fixed_sum + full_sum > avail) {
        int title_budget = avail - fixed_sum;
        if (title_budget < 0)
            title_budget = 0;
        int cur_sum = full_sum;
        while (cur_sum > title_budget) {
            int max_w = 0;
            for (size_t i = 0; i < ntabs; i++) {
                if (alloc_w[i] > max_w)
                    max_w = alloc_w[i];
            }
            if (max_w <= 0)
                break;
            for (size_t i = 0; i < ntabs && cur_sum > title_budget; i++) {
                if (alloc_w[i] == max_w) {
                    alloc_w[i]--;
                    cur_sum--;
                }
            }
        }
    }

    tam_str_t tabs_str = tam_str_new("");
    for (size_t i = 0; i < ntabs; i++) {
        bool act = (i == app->active_tab);
        char num_label[16];
        if (i == 9)
            snprintf(num_label, sizeof(num_label), "0");
        else
            snprintf(num_label, sizeof(num_label), "%zu", i + 1);

        char disp_title[128];
        truncate_text_cols(app->tabs[i].title, alloc_w[i],
                           app->opts.ascii, disp_title, sizeof(disp_title));

        const char *sep = (app->sidebar_visible && i == 0) ? "" : "|";
        if (app->opts.color) {
            tabs_str = tam_str_cat_printf(tabs_str, "%s%s %s:%s \033[0m%s",
                sep,
                act ? sgr_header_focused(app) : "\033[1;38;5;252;48;5;238m",
                num_label, disp_title, bar_bg);
        } else {
            tabs_str = tam_str_cat_printf(tabs_str, "%s%s%s:%s%s",
                sep, act ? "[" : " ", num_label, disp_title, act ? "]" : " ");
        }
    }

    if (app->sidebar_visible) {
        int sep_x = app->sidebar_width + 1;
        int tab_x = app->sidebar_width + 2;
        const char *sep_sgr = (app->opts.color && !app->opts.linux_vt)
                              ? "\033[38;5;244;48;5;236m"
                              : (app->opts.color ? bar_bg : "");
        frame_move_write(frame, 1, 1, prefix, app->sidebar_width,
                         app->opts.color ? bar_bg : "", true);
        frame_move_write(frame, 1, sep_x, bx->v, 1, sep_sgr, false);
        frame_move_write(frame, 1, tab_x, tabs_str, avail,
                         app->opts.color ? bar_bg : "", true);
    } else {
        tam_str_t bar = tam_str_new(prefix);
        bar = tam_str_cat(bar, tabs_str);
        frame_move_write(frame, 1, 1, bar, cols,
                         app->opts.color ? bar_bg : "", true);
        tam_str_free(bar);
    }

    tam_str_free(tabs_str);
    tam_str_free(prefix);
}

static void render_sidebar(knox_app_t *app, tam_str_t *frame,
                           int x, int y, int w, int h)
{
    const struct tam_render_box *bx = tam_render_box_chars(app->opts.ascii);

    if (app->sidebar_mode == SIDEBAR_SOURCE_CONTROL) {
        int repos_h = (h * 55) / 100;
        if (repos_h < 4)
            repos_h = h / 2;
        int changes_h = h - repos_h;

        /* 1. Repositories Header */
        bool focus_repos = (app->focus == FOCUS_SIDEBAR_REPOS);
        char hdr[256];
        snprintf(hdr, sizeof(hdr), " %s REPOSITORIES (%zu) [f Fetch, s Sync, Alt+A All]",
                 bx->arrow_d, app->ws.count);
        const char *hdr_sgr = focus_repos
                              ? sgr_header_focused(app)
                              : sgr_header_unfocused(app);
        frame_move_write(frame, y, x, hdr, w, hdr_sgr, true);

        /* Compute widest repo name so names like config-fred-tamlinux fit */
        int name_col_w = 18;
        for (size_t i = 0; i < app->ws.count; i++) {
            int nlen = (int)strlen(app->ws.repos[i].name);
            if (nlen > name_col_w)
                name_col_w = nlen;
        }
        int max_name_w = w - 14;
        if (max_name_w < 14)
            max_name_w = 14;
        if (name_col_w > max_name_w)
            name_col_w = max_name_w;

        /* Repositories rows */
        int list_rows = repos_h - 1;
        if (list_rows > 0) {
            if (app->active_repo_idx < app->repo_top)
                app->repo_top = app->active_repo_idx;
            if (app->active_repo_idx >= app->repo_top + (size_t)list_rows)
                app->repo_top = app->active_repo_idx - (size_t)list_rows + 1;

            for (int r = 0; r < list_rows; r++) {
                size_t idx = app->repo_top + (size_t)r;
                int row_y = y + 1 + r;
                if (idx >= app->ws.count) {
                    frame_move_write(frame, row_y, x, "", w, "", true);
                    continue;
                }
                const knox_repo_t *repo = &app->ws.repos[idx];
                bool sel = (idx == app->active_repo_idx);

                char sync_badge[64] = "";
                if (repo->ahead > 0 || repo->behind > 0) {
                    snprintf(sync_badge, sizeof(sync_badge), " %s%d %s%d",
                             app->opts.ascii ? "^" : "\xe2\x86\x91", repo->ahead,
                             app->opts.ascii ? "v" : "\xe2\x86\x93", repo->behind);
                }

                char dirty_badge[32] = "";
                if (repo->dirty)
                    snprintf(dirty_badge, sizeof(dirty_badge), "* (%zu)", repo->change_count);

                char line[512];
                if (app->opts.color && !sel) {
                    snprintf(line, sizeof(line),
                             " %-*.*s \033[1;36m%s\033[0m\033[1;33m%s\033[0m\033[1;35m%s\033[0m",
                             name_col_w, name_col_w, repo->name,
                             repo->branch ? repo->branch : "?",
                             dirty_badge, sync_badge);
                } else {
                    snprintf(line, sizeof(line), " %-*.*s %s%s%s",
                             name_col_w, name_col_w, repo->name,
                             repo->branch ? repo->branch : "?",
                             dirty_badge, sync_badge);
                }

                const char *row_bg = sel ? sgr_row_selected(app, focus_repos) : "";
                frame_move_write(frame, row_y, x, line, w, row_bg, true);
            }
        }

        /* 2. Changes Header */
        int cy = y + repos_h;
        bool focus_chg = (app->focus == FOCUS_SIDEBAR_CHANGES);
        const knox_repo_t *act_repo = (app->active_repo_idx < app->ws.count)
                                      ? &app->ws.repos[app->active_repo_idx]
                                      : NULL;
        snprintf(hdr, sizeof(hdr), " %s CHANGES: %s (%zu) [Space Stage, Alt+C]",
                 bx->arrow_d,
                 act_repo ? act_repo->name : "-",
                 act_repo ? act_repo->change_count : 0);
        const char *chdr_sgr = focus_chg
                               ? sgr_header_focused(app)
                               : sgr_header_unfocused(app);
        frame_move_write(frame, cy, x, hdr, w, chdr_sgr, true);

        int chg_rows = changes_h - 1;
        if (chg_rows > 0) {
            size_t ccount = act_repo ? act_repo->change_count : 0;
            if (app->change_cursor >= ccount && ccount > 0)
                app->change_cursor = ccount - 1;
            if (app->change_cursor < app->change_top)
                app->change_top = app->change_cursor;
            if (app->change_cursor >= app->change_top + (size_t)chg_rows)
                app->change_top = app->change_cursor - (size_t)chg_rows + 1;

            for (int r = 0; r < chg_rows; r++) {
                size_t idx = app->change_top + (size_t)r;
                int row_y = cy + 1 + r;
                if (!act_repo || idx >= ccount) {
                    if (r == 0 && ccount == 0) {
                        frame_move_write(frame, row_y, x,
                                         "   (working tree clean)", w,
                                         sgr_muted(app), true);
                    } else {
                        frame_move_write(frame, row_y, x, "", w, "", true);
                    }
                    continue;
                }
                const knox_file_change_t *fc = &act_repo->changes[idx];
                bool sel = (idx == app->change_cursor);
                char st_let = (char)((fc->worktree_status != KNOX_CHANGE_NONE)
                                     ? fc->worktree_status
                                     : fc->index_status);
                const char *box = fc->staged ? "[+]" : "[ ]";
                char line[512];
                if (app->opts.color && !sel) {
                    const char *sc = (st_let == 'M') ? "\033[1;33m" :
                                     (st_let == 'A') ? "\033[1;32m" :
                                     (st_let == 'D') ? "\033[1;31m" : "\033[1;36m";
                    snprintf(line, sizeof(line), " %s %s%c\033[0m %s",
                             box, sc, st_let, fc->path);
                } else {
                    snprintf(line, sizeof(line), " %s %c %s",
                             box, st_let, fc->path);
                }
                const char *row_bg = sel ? sgr_row_selected(app, focus_chg) : "";
                frame_move_write(frame, row_y, x, line, w, row_bg, true);
            }
        }
    } else {
        /* SIDEBAR_EXPLORER */
        bool focus_exp = (app->focus == FOCUS_SIDEBAR_EXPLORER);
        char hdr[256];
        if (app->explorer.filter[0] != '\0') {
            size_t matches = app->explorer.flat_count > 0
                             ? app->explorer.flat_count - 1 : 0;
            snprintf(hdr, sizeof(hdr),
                     " %s EXPLORER [filter: \"%s\"] (%zu) [Esc Clear]",
                     bx->arrow_d, app->explorer.filter, matches);
        } else {
            snprintf(hdr, sizeof(hdr), " %s EXPLORER (%zu items) [/ Filter]",
                     bx->arrow_d, app->explorer.flat_count);
        }
        const char *hdr_sgr = focus_exp
                              ? sgr_header_focused(app)
                              : sgr_header_unfocused(app);
        frame_move_write(frame, y, x, hdr, w, hdr_sgr, true);

        int rows = h - 1;
        if (rows > 0) {
            if (app->explorer_cursor >= app->explorer.flat_count &&
                app->explorer.flat_count > 0)
                app->explorer_cursor = app->explorer.flat_count - 1;
            if (app->explorer_cursor < app->explorer_top)
                app->explorer_top = app->explorer_cursor;
            if (app->explorer_cursor >= app->explorer_top + (size_t)rows)
                app->explorer_top = app->explorer_cursor - (size_t)rows + 1;

            for (int r = 0; r < rows; r++) {
                size_t idx = app->explorer_top + (size_t)r;
                int row_y = y + 1 + r;
                if (idx >= app->explorer.flat_count) {
                    frame_move_write(frame, row_y, x, "", w, "", true);
                    continue;
                }
                const knox_tree_node_t *n = app->explorer.flat[idx];
                bool sel = (idx == app->explorer_cursor);

                char indent[64] = "";
                int ind_spaces = n->depth * 2;
                if (ind_spaces > 32)
                    ind_spaces = 32;
                memset(indent, ' ', (size_t)ind_spaces);
                indent[ind_spaces] = '\0';

                const char *marker = "  ";
                if (n->is_dir) {
                    marker = n->expanded
                             ? (app->opts.ascii ? "v " : "\xe2\x96\xbe ")
                             : (app->opts.ascii ? "> " : "\xe2\x96\xb8 ");
                }

                char status_tail[32] = "";
                if (!n->is_dir && n->git_status != KNOX_CHANGE_NONE) {
                    snprintf(status_tail, sizeof(status_tail), " [%c]",
                             (char)n->git_status);
                } else if (n->is_dir && n->has_dirty_desc) {
                    snprintf(status_tail, sizeof(status_tail), " %s",
                             app->opts.ascii ? "*" : "\xe2\x97\x8f");
                }

                char line[512];
                if (app->opts.color && !sel) {
                    const char *nc = n->is_dir ? "\033[1;36m" : "";
                    const char *tc = (n->git_status != KNOX_CHANGE_NONE || n->has_dirty_desc)
                                     ? "\033[1;33m" : "";
                    snprintf(line, sizeof(line), " %s%s%s%s%s\033[0m%s%s\033[0m",
                             indent, marker, nc, n->name,
                             n->is_dir ? "/" : "", tc, status_tail);
                } else {
                    snprintf(line, sizeof(line), " %s%s%s%s%s",
                             indent, marker, n->name,
                             n->is_dir ? "/" : "", status_tail);
                }

                const char *row_bg = sel ? sgr_row_selected(app, focus_exp) : "";
                frame_move_write(frame, row_y, x, line, w, row_bg, true);
            }
        }
    }
}

static void format_commit_date(int64_t ts, char *out, size_t cap)
{
    if (ts <= 0) {
        snprintf(out, cap, "-");
        return;
    }
    time_t t = (time_t)ts;
    struct tm tm_val;
    if (!localtime_r(&t, &tm_val)) {
        snprintf(out, cap, "-");
        return;
    }
    strftime(out, cap, "%d %b %Y %H:%M", &tm_val);
}

static void render_graph_pane(knox_app_t *app, knox_pane_t *p,
                              tam_str_t *frame, bool is_focused)
{
    if (app->ws.count == 0) {
        frame_move_write(frame, p->y, p->x,
                         " No Git repositories found in workspace.",
                         p->w, "", true);
        return;
    }
    if (p->repo_idx >= app->ws.count)
        p->repo_idx = app->active_repo_idx;

    app_ensure_repo_graph(app, p->repo_idx, p->graph_all_branches,
                          p->graph_show_remotes, false);
    const knox_repo_t *repo = &app->ws.repos[p->repo_idx];

    /* 1. Control Header (Repo, Branch Filter, Remotes, Count) */
    char ctrl[512];
    snprintf(ctrl, sizeof(ctrl),
             " Git Graph | Repo: %s (%s) | Branches: %s [B] | Commits: %zu [Enter: Inspect]",
             repo->name, repo->branch ? repo->branch : "?",
             p->graph_all_branches ? "Show All" : "Current (HEAD)",
             app->graph.commit_count);
    const char *hdr_sgr = is_focused
                          ? sgr_header_focused(app)
                          : sgr_header_unfocused(app);
    frame_move_write(frame, p->y, p->x, ctrl, p->w, hdr_sgr, true);
    if (p->h < 2)
        return;

    /* 2. Column Headers: Graph | Description | Date | Author | Commit */
    int date_w = 17;
    int author_w = 14;
    int hash_w = 8;
    int right_cols = date_w + 1 + author_w + 1 + hash_w + 1;
    int desc_avail = p->w - right_cols;
    if (desc_avail < 20)
        desc_avail = p->w;

    char col_hdr[512];
    if (p->w >= 72) {
        snprintf(col_hdr, sizeof(col_hdr), " %-*.*s %-*s %-*s %-*s",
                 desc_avail - 1, desc_avail - 1, "Graph & Description",
                 date_w, "Date", author_w, "Author", hash_w, "Commit");
    } else {
        snprintf(col_hdr, sizeof(col_hdr), " Graph & Description");
    }
    const char *col_hdr_sgr = app->opts.color
                              ? (app->opts.linux_vt ? "\033[1;4m" : "\033[1;38;5;253;48;5;235m")
                              : "";
    frame_move_write(frame, p->y + 1, p->x, col_hdr, p->w, col_hdr_sgr, true);

    /* 3. Commit rows */
    int list_h = p->h - 2;
    if (list_h <= 0)
        return;

    if (p->graph_cursor >= app->graph.commit_count &&
        app->graph.commit_count > 0)
        p->graph_cursor = app->graph.commit_count - 1;
    if (p->graph_cursor < p->graph_top)
        p->graph_top = p->graph_cursor;
    if (p->graph_cursor >= p->graph_top + (size_t)list_h)
        p->graph_top = p->graph_cursor - (size_t)list_h + 1;

    for (int r = 0; r < list_h; r++) {
        size_t idx = p->graph_top + (size_t)r;
        int row_y = p->y + 2 + r;
        if (idx >= app->graph.commit_count) {
            frame_move_write(frame, row_y, p->x, "", p->w, "", true);
            continue;
        }

        const knox_commit_t *c = &app->graph.commits[idx];
        bool sel = (idx == p->graph_cursor);

        char gcells[512];
        int gvis = 0;
        knox_graph_format_row(&app->graph, idx, 24,
                              app->opts.color && !sel, app->opts.linux_vt,
                              gcells, sizeof(gcells), &gvis);

        /* Format inline ref badges: [HEAD -> dev] [origin/dev] [tag: v0.1.0] */
        tam_str_t refs = tam_str_new("");
        for (size_t h = 0; h < c->head_count; h++) {
            bool is_cur = (repo->branch && strcmp(c->heads[h], repo->branch) == 0);
            if (app->opts.color && !sel) {
                refs = tam_str_cat_printf(refs, "\033[1;36m[%s%s]\033[0m ",
                                          is_cur ? "* " : "", c->heads[h]);
            } else {
                refs = tam_str_cat_printf(refs, "[%s%s] ",
                                          is_cur ? "* " : "", c->heads[h]);
            }
        }
        for (size_t rm = 0; rm < c->remote_count; rm++) {
            if (app->opts.color && !sel)
                refs = tam_str_cat_printf(refs, "\033[35m[%s]\033[0m ", c->remotes[rm]);
            else
                refs = tam_str_cat_printf(refs, "[%s] ", c->remotes[rm]);
        }
        for (size_t t = 0; t < c->tag_count; t++) {
            if (app->opts.color && !sel)
                refs = tam_str_cat_printf(refs, "\033[1;33m[tag: %s]\033[0m ", c->tags[t]);
            else
                refs = tam_str_cat_printf(refs, "[tag: %s] ", c->tags[t]);
        }

        tam_str_t left_part = tam_str_new(" ");
        left_part = tam_str_cat(left_part, gcells);
        left_part = tam_str_cat(left_part, " ");
        left_part = tam_str_cat(left_part, refs);
        if (c->is_uncommitted && app->opts.color && !sel) {
            left_part = tam_str_cat_printf(left_part, "\033[3;33m%s\033[0m",
                                           c->subject ? c->subject : "");
        } else if (c->is_head && app->opts.color && !sel) {
            left_part = tam_str_cat_printf(left_part, "\033[1m%s\033[0m",
                                           c->subject ? c->subject : "");
        } else {
            left_part = tam_str_cat(left_part, c->subject ? c->subject : "");
        }
        tam_str_free(refs);

        const char *row_bg = sel ? sgr_row_selected(app, is_focused) : "";

        if (p->w >= 72) {
            char clipped_left[2048];
            format_clipped_padded(clipped_left, sizeof(clipped_left),
                                  left_part, desc_avail, true);
            char date_str[32];
            format_commit_date(c->timestamp, date_str, sizeof(date_str));
            char right_meta[256];
            if (app->opts.color && !sel) {
                snprintf(right_meta, sizeof(right_meta),
                         " %s%-*.*s\033[0m \033[32m%-*.*s\033[0m \033[1;33m%-8.8s\033[0m",
                         sgr_muted(app), date_w, date_w, date_str,
                         author_w, author_w, c->author ? c->author : "",
                         c->hash);
            } else {
                snprintf(right_meta, sizeof(right_meta),
                         " %-*.*s %-*.*s %-8.8s",
                         date_w, date_w, date_str,
                         author_w, author_w, c->author ? c->author : "",
                         c->hash);
            }
            tam_str_t full_row = tam_str_new(clipped_left);
            full_row = tam_str_cat(full_row, right_meta);
            frame_move_write(frame, row_y, p->x, full_row, p->w, row_bg, true);
            tam_str_free(full_row);
        } else {
            frame_move_write(frame, row_y, p->x, left_part, p->w, row_bg, true);
        }
        tam_str_free(left_part);
    }
}

static void render_commit_detail_pane(knox_app_t *app, knox_pane_t *p,
                                      tam_str_t *frame, bool is_focused)
{
    if (p->repo_idx >= app->ws.count)
        p->repo_idx = app->active_repo_idx;
    app_ensure_commit_details(app, p->repo_idx, p->inspect_commit_idx);

    const knox_repo_t *repo = &app->ws.repos[p->repo_idx];
    const knox_commit_details_t *d = &app->details;

    char hdr[512];
    snprintf(hdr, sizeof(hdr),
             " Commit Details: %.8s (%s) | %zu file(s) changed [Enter: View Diff, v: View File, Ctrl+D: Close]",
             d->hash, repo->name, d->file_count);
    const char *hdr_sgr = is_focused
                          ? sgr_header_focused(app)
                          : sgr_header_unfocused(app);
    frame_move_write(frame, p->y, p->x, hdr, p->w, hdr_sgr, true);
    if (p->h < 2)
        return;

    /* Summary lines at top of inspector */
    char date_str[32];
    format_commit_date(d->author_date, date_str, sizeof(date_str));
    char meta_line[512];
    snprintf(meta_line, sizeof(meta_line),
             " Commit: %s | Author: %s <%s> | Date: %s",
             d->hash, d->author ? d->author : "",
             d->author_email ? d->author_email : "", date_str);
    frame_move_write(frame, p->y + 1, p->x, meta_line, p->w,
                     app->opts.color ? "\033[36m" : "", true);

    if (p->h < 3)
        return;

    /* Subject / first line of body */
    char subj_line[512] = " ";
    if (d->body) {
        const char *nl = strchr(d->body, '\n');
        size_t slen = nl ? (size_t)(nl - d->body) : strlen(d->body);
        snprintf(subj_line, sizeof(subj_line), " Message: %.*s",
                 (int)slen, d->body);
    }
    frame_move_write(frame, p->y + 2, p->x, subj_line, p->w,
                     app->opts.color ? "\033[1m" : "", true);

    int list_h = p->h - 3;
    if (list_h <= 0)
        return;

    if (p->inspect_file_cursor >= d->file_count && d->file_count > 0)
        p->inspect_file_cursor = d->file_count - 1;
    if (p->inspect_file_cursor < p->inspect_file_top)
        p->inspect_file_top = p->inspect_file_cursor;
    if (p->inspect_file_cursor >= p->inspect_file_top + (size_t)list_h)
        p->inspect_file_top = p->inspect_file_cursor - (size_t)list_h + 1;

    for (int r = 0; r < list_h; r++) {
        size_t idx = p->inspect_file_top + (size_t)r;
        int row_y = p->y + 3 + r;
        if (idx >= d->file_count) {
            frame_move_write(frame, row_y, p->x, "", p->w, "", true);
            continue;
        }
        const knox_file_change_t *fc = &d->files[idx];
        bool sel = (idx == p->inspect_file_cursor);
        char st_let = (char)((fc->index_status != KNOX_CHANGE_NONE)
                             ? fc->index_status
                             : fc->worktree_status);

        char stats[64] = "";
        if (fc->additions >= 0 || fc->deletions >= 0) {
            if (app->opts.color && !sel) {
                snprintf(stats, sizeof(stats),
                         " \033[32m+%d\033[0m \033[31m-%d\033[0m",
                         fc->additions >= 0 ? fc->additions : 0,
                         fc->deletions >= 0 ? fc->deletions : 0);
            } else {
                snprintf(stats, sizeof(stats), " +%d -%d",
                         fc->additions >= 0 ? fc->additions : 0,
                         fc->deletions >= 0 ? fc->deletions : 0);
            }
        }

        char line[512];
        if (fc->old_path) {
            snprintf(line, sizeof(line), "   [%c] %s -> %s%s",
                     st_let, fc->old_path, fc->path, stats);
        } else if (app->opts.color && !sel) {
            const char *sc = (st_let == 'A') ? "\033[32m" :
                             (st_let == 'D') ? "\033[31m" : "\033[33m";
            snprintf(line, sizeof(line), "   %s[%c]\033[0m %s%s",
                     sc, st_let, fc->path, stats);
        } else {
            snprintf(line, sizeof(line), "   [%c] %s%s",
                     st_let, fc->path, stats);
        }

        const char *row_bg = sel ? sgr_row_selected(app, is_focused) : "";
        frame_move_write(frame, row_y, p->x, line, p->w, row_bg, true);
    }
}

static const char *view_style_sgr(unsigned char style)
{
    switch (style) {
    case TAM_RENDER_STYLE_H1:         return "\033[1;4;36m";
    case TAM_RENDER_STYLE_H2:         return "\033[1;36m";
    case TAM_RENDER_STYLE_H3:         return "\033[1;34m";
    case TAM_RENDER_STYLE_BOLD:       return "\033[1m";
    case TAM_RENDER_STYLE_ITALIC:     return "\033[3m";
    case TAM_RENDER_STYLE_STRIKE:     return "\033[9m";
    case TAM_RENDER_STYLE_CODE:       return "\033[33m";
    case TAM_RENDER_STYLE_LINK:       return "\033[4;34m";
    case TAM_RENDER_STYLE_LINK_URL:   return "\033[90m";
    case TAM_RENDER_STYLE_LINK_CUR:   return "\033[1;7;36m";
    case TAM_RENDER_STYLE_QUOTE:      return "\033[32m";
    case TAM_RENDER_STYLE_RULE:       return "\033[90m";
    case TAM_RENDER_STYLE_TABLE_HDR:  return "\033[1;36m";
    case TAM_RENDER_STYLE_NODE:       return "\033[1;36m";
    case TAM_RENDER_STYLE_EDGE:       return "\033[33m";
    default:                    return "";
    }
}

static unsigned char lookup_rendered_style(const struct tam_render_styles *st,
                                           off_t byte_off)
{
    if (!st || st->count == 0)
        return TAM_RENDER_STYLE_NONE;
    /* Binary search for last span with start <= byte_off < end */
    for (size_t i = 0; i < st->count; i++) {
        if (st->spans[i].start <= byte_off && byte_off < st->spans[i].end)
            return st->spans[i].style;
        if (st->spans[i].start > byte_off)
            break;
    }
    return TAM_RENDER_STYLE_NONE;
}

static void render_viewer_sbs_diff(knox_app_t *app, knox_pane_t *p,
                                   tam_str_t *frame, bool is_focused,
                                   int body_h)
{
    knox_viewer_doc_t *doc = &p->doc;
    knox_viewer_build_sbs(doc);

    const struct tam_render_box *bx = tam_render_box_chars(app->opts.ascii);
    int left_w = (p->w - 1) / 2;
    if (left_w < 10)
        left_w = 10;
    int right_w = p->w - 1 - left_w;
    if (right_w < 10)
        right_w = 10;
    int sep_x = p->x + left_w;
    int right_x = sep_x + 1;

    const char *del_sgr = app->opts.color
                          ? (app->opts.linux_vt ? "\033[1;31m" : "\033[38;5;217;48;5;52m")
                          : "";
    const char *add_sgr = app->opts.color
                          ? (app->opts.linux_vt ? "\033[1;32m" : "\033[38;5;157;48;5;22m")
                          : "";
    const char *hunk_sgr = app->opts.color
                           ? (app->opts.linux_vt ? "\033[1;36m" : "\033[1;38;5;81;48;5;236m")
                           : "";
    const char *hdr_line_sgr = app->opts.color
                               ? (app->opts.linux_vt ? "\033[1;33m" : "\033[1;38;5;228m")
                               : "";

    for (int r = 0; r < body_h; r++) {
        size_t idx = doc->top_line + (size_t)r;
        int row_y = p->y + 1 + r;
        if (idx >= doc->sbs_count) {
            frame_move_write(frame, row_y, p->x, "~", left_w,
                             app->opts.color ? "\033[90m" : "", true);
            frame_move_write(frame, row_y, sep_x, bx->v, 1,
                             app->opts.color ? "\033[90m" : "", false);
            frame_move_write(frame, row_y, right_x, "~", right_w,
                             app->opts.color ? "\033[90m" : "", true);
            continue;
        }

        const knox_sbs_row_t *sr = &doc->sbs_rows[idx];
        bool is_cursor = (idx == doc->cursor_line);
        const char *cursor_sgr = is_cursor ? sgr_row_selected(app, is_focused) : NULL;

        if (sr->kind == KNOX_SBS_HEADER || sr->kind == KNOX_SBS_HUNK) {
            tam_str_t banner = tam_str_new(" ");
            if (doc->raw_text && sr->old_len > 0)
                banner = tam_str_cat_len(banner, doc->raw_text + sr->old_off, sr->old_len);
            const char *bg = cursor_sgr ? cursor_sgr :
                             (sr->kind == KNOX_SBS_HUNK ? hunk_sgr : hdr_line_sgr);
            frame_move_write(frame, row_y, p->x, banner, p->w, bg, true);
            tam_str_free(banner);
            continue;
        }

        /* Left cell (Old) */
        tam_str_t lstr = tam_str_new("");
        if (sr->old_lineno > 0) {
            char mark = sr->old_changed ? '-' : ' ';
            if (app->opts.color && !is_cursor && !sr->old_changed)
                lstr = tam_str_cat_printf(lstr, "%s%4d\033[0m%c", sgr_muted(app), sr->old_lineno, mark);
            else
                lstr = tam_str_cat_printf(lstr, "%4d%c", sr->old_lineno, mark);

            size_t skip = (size_t)doc->left_col;
            if (doc->raw_text && skip < sr->old_len) {
                const char *src = doc->raw_text + sr->old_off + skip;
                size_t slen = sr->old_len - skip;
                for (size_t b = 0; b < slen; b++) {
                    if (src[b] == '\t')
                        lstr = tam_str_cat(lstr, "    ");
                    else
                        lstr = tam_str_cat_len(lstr, src + b, 1);
                }
            }
        } else {
            lstr = tam_str_cat(lstr, "     ");
        }
        const char *l_bg = cursor_sgr ? cursor_sgr :
                           (sr->old_changed ? del_sgr : "");
        frame_move_write(frame, row_y, p->x, lstr, left_w, l_bg, true);
        tam_str_free(lstr);

        /* Center divider */
        frame_move_write(frame, row_y, sep_x, bx->v, 1,
                         cursor_sgr ? cursor_sgr : (app->opts.color ? "\033[90m" : ""),
                         false);

        /* Right cell (New) */
        tam_str_t rstr = tam_str_new("");
        if (sr->new_lineno > 0) {
            char mark = sr->new_changed ? '+' : ' ';
            if (app->opts.color && !is_cursor && !sr->new_changed)
                rstr = tam_str_cat_printf(rstr, "%s%4d\033[0m%c", sgr_muted(app), sr->new_lineno, mark);
            else
                rstr = tam_str_cat_printf(rstr, "%4d%c", sr->new_lineno, mark);

            size_t skip = (size_t)doc->left_col;
            if (doc->raw_text && skip < sr->new_len) {
                const char *src = doc->raw_text + sr->new_off + skip;
                size_t slen = sr->new_len - skip;
                for (size_t b = 0; b < slen; b++) {
                    if (src[b] == '\t')
                        rstr = tam_str_cat(rstr, "    ");
                    else
                        rstr = tam_str_cat_len(rstr, src + b, 1);
                }
            }
        } else {
            rstr = tam_str_cat(rstr, "     ");
        }
        const char *r_bg = cursor_sgr ? cursor_sgr :
                           (sr->new_changed ? add_sgr : "");
        frame_move_write(frame, row_y, right_x, rstr, right_w, r_bg, true);
        tam_str_free(rstr);
    }
}

static void render_viewer_pane(knox_app_t *app, knox_pane_t *p,
                               tam_str_t *frame, bool is_focused)
{
    knox_viewer_doc_t *doc = &p->doc;
    if (doc->render_mode)
        knox_viewer_ensure_rendered(doc, p->w > 4 ? p->w - 2 : 40, app->opts.ascii);
    if (doc->kind == KNOX_DOC_DIFF && doc->side_by_side)
        knox_viewer_build_sbs(doc);

    size_t total_lines = (doc->kind == KNOX_DOC_DIFF && doc->side_by_side)
                         ? doc->sbs_count
                         : (doc->render_mode ? doc->rend_line_count : doc->line_count);

    /* 1. Breadcrumb & Mode Header */
    char hdr[512];
    const char *mode_label = (doc->kind == KNOX_DOC_DIFF)
                             ? (doc->side_by_side ? "Side-by-Side Diff [d/R]" : "Unified Diff [d/R]")
                             : (doc->render_mode ? "Rendered [R]" : "Raw Syntax [R]");
    snprintf(hdr, sizeof(hdr),
             " %s%s | Mode: %s | Lang: %s | Line %zu/%zu [Alt+E Edit]",
             p->file_stale ? "[Stale] " : "",
             doc->breadcrumb ? doc->breadcrumb : (doc->title ? doc->title : "File"),
             mode_label,
             knox_syntax_lang_name(doc->lang),
             total_lines ? doc->cursor_line + 1 : 0,
             total_lines);
    const char *hdr_sgr = is_focused
                          ? sgr_header_focused(app)
                          : sgr_header_unfocused(app);
    frame_move_write(frame, p->y, p->x, hdr, p->w, hdr_sgr, true);

    int body_h = p->h - 1;
    if (body_h <= 0)
        return;

    if (doc->cursor_line >= total_lines && total_lines > 0)
        doc->cursor_line = total_lines - 1;
    if (doc->cursor_line < doc->top_line)
        doc->top_line = doc->cursor_line;
    if (doc->cursor_line >= doc->top_line + (size_t)body_h)
        doc->top_line = doc->cursor_line - (size_t)body_h + 1;

    if (doc->kind == KNOX_DOC_DIFF && doc->side_by_side) {
        render_viewer_sbs_diff(app, p, frame, is_focused, body_h);
        return;
    }

    uint8_t tok_buf[4096];

    for (int r = 0; r < body_h; r++) {
        size_t idx = doc->top_line + (size_t)r;
        int row_y = p->y + 1 + r;
        if (idx >= total_lines) {
            frame_move_write(frame, row_y, p->x, "~", p->w,
                             app->opts.color ? "\033[90m" : "", true);
            continue;
        }

        bool is_cursor = (idx == doc->cursor_line);
        tam_str_t row_str = tam_str_new("");

        if (doc->render_mode) {
            /* Rendered Markdown / HTML / Mermaid row */
            const char *base = doc->rendered_buf.data ? doc->rendered_buf.data : "";
            size_t off = doc->rend_offsets[idx];
            size_t len = doc->rend_lengths[idx];
            row_str = tam_str_cat(row_str, " ");

            if (app->opts.color && !is_cursor) {
                unsigned char cur_st = TAM_RENDER_STYLE_NONE;
                for (size_t b = 0; b < len; b++) {
                    unsigned char st = lookup_rendered_style(
                        &doc->rendered_buf.styles, (off_t)(off + b));
                    if (st != cur_st) {
                        row_str = tam_str_cat(row_str, "\033[0m");
                        row_str = tam_str_cat(row_str, view_style_sgr(st));
                        cur_st = st;
                    }
                    row_str = tam_str_cat_len(row_str, base + off + b, 1);
                }
                if (cur_st != TAM_RENDER_STYLE_NONE)
                    row_str = tam_str_cat(row_str, "\033[0m");
            } else {
                row_str = tam_str_cat_len(row_str, base + off, len);
            }
        } else {
            /* Raw syntax-highlighted row with line numbers */
            if (doc->kind == KNOX_DOC_FILE) {
                if (app->opts.color && !is_cursor)
                    row_str = tam_str_cat_printf(row_str, "%s%4zu \033[0m", sgr_muted(app), idx + 1);
                else
                    row_str = tam_str_cat_printf(row_str, "%4zu ", idx + 1);
            } else {
                row_str = tam_str_cat(row_str, " ");
            }

            const char *line = doc->raw_text + doc->line_offsets[idx];
            size_t len = doc->line_lengths[idx];
            size_t skip = (size_t)doc->left_col;
            if (skip < len) {
                const char *vis_line = line + skip;
                size_t vis_len = len - skip;
                if (vis_len > sizeof(tok_buf))
                    vis_len = sizeof(tok_buf);

                knox_lex_state_t in_st = doc->line_states
                                         ? (knox_lex_state_t)doc->line_states[idx]
                                         : KNOX_LEX_NORMAL;
                knox_syntax_highlight_line(doc->lang, in_st,
                                           vis_line, vis_len, tok_buf);

                if (app->opts.color && !is_cursor) {
                    uint8_t cur_tok = KNOX_TOK_NORMAL;
                    for (size_t b = 0; b < vis_len; b++) {
                        uint8_t tk = tok_buf[b];
                        if (tk != cur_tok) {
                            row_str = tam_str_cat(row_str, "\033[0m");
                            row_str = tam_str_cat(row_str,
                                knox_syntax_token_sgr((knox_token_kind_t)tk,
                                                      app->opts.linux_vt));
                            cur_tok = tk;
                        }
                        if (vis_line[b] == '\t')
                            row_str = tam_str_cat(row_str, "    ");
                        else
                            row_str = tam_str_cat_len(row_str, vis_line + b, 1);
                    }
                    if (cur_tok != KNOX_TOK_NORMAL)
                        row_str = tam_str_cat(row_str, "\033[0m");
                } else {
                    for (size_t b = 0; b < vis_len; b++) {
                        if (vis_line[b] == '\t')
                            row_str = tam_str_cat(row_str, "    ");
                        else
                            row_str = tam_str_cat_len(row_str, vis_line + b, 1);
                    }
                }
            }
        }

        const char *row_bg = is_cursor ? sgr_row_selected(app, is_focused) : "";
        frame_move_write(frame, row_y, p->x, row_str, p->w, row_bg, true);
        tam_str_free(row_str);
    }
}

static void render_split_tree(knox_app_t *app, knox_split_node_t *node,
                              tam_str_t *frame)
{
    if (!node)
        return;
    const struct tam_render_box *bx = tam_render_box_chars(app->opts.ascii);

    if (node->type == KNOX_NODE_LEAF) {
        knox_pane_t *p = node->pane;
        if (!p)
            return;
        knox_tab_t *tab = app_current_tab(app);
        bool is_focused = (app->focus == FOCUS_MAIN_PANE &&
                           tab && tab->active_pane == p);
        if (p->content == KNOX_PANE_GRAPH)
            render_graph_pane(app, p, frame, is_focused);
        else if (p->content == KNOX_PANE_COMMIT_DETAIL)
            render_commit_detail_pane(app, p, frame, is_focused);
        else
            render_viewer_pane(app, p, frame, is_focused);
        return;
    }

    render_split_tree(app, node->child1, frame);
    if (node->type == KNOX_NODE_SPLIT_V) {
        int sep_x = node->child1->x + node->child1->w;
        for (int r = 0; r < node->h; r++) {
            frame_move_write(frame, node->y + r, sep_x, bx->v, 1,
                             app->opts.color ? "\033[90m" : "", false);
        }
    } else if (node->type == KNOX_NODE_SPLIT_H) {
        int sep_y = node->child1->y + node->child1->h;
        tam_str_t hline = tam_str_new("");
        for (int c = 0; c < node->w; c++)
            hline = tam_str_cat(hline, bx->h);
        frame_move_write(frame, sep_y, node->x, hline, node->w,
                         app->opts.color ? "\033[90m" : "", false);
        tam_str_free(hline);
    }
    render_split_tree(app, node->child2, frame);
}

static void render_modal_border(const knox_app_t *app, tam_str_t *frame,
                                int rows, int cols,
                                int x0, int y0, int box_w, int box_h)
{
    if (rows < 1 || cols < 1 || box_w < 1 || box_h < 1)
        return;

    const char *border_bg = app->opts.color
                            ? (app->opts.linux_vt ? "\033[40m" : "\033[48;5;16m")
                            : "";
    const int pad_x = 2;
    int left_w = (x0 > 1) ? ((x0 - 1 < pad_x) ? (x0 - 1) : pad_x) : 0;
    int right_avail = cols - (x0 + box_w - 1);
    int right_w = (right_avail > 0)
                  ? ((right_avail < pad_x) ? right_avail : pad_x) : 0;
    int outer_x = x0 - left_w;
    int outer_w = left_w + box_w + right_w;

    if (outer_w > 0) {
        if (y0 > 1)
            frame_move_write(frame, y0 - 1, outer_x, "", outer_w, border_bg, true);
        if (y0 + box_h <= rows)
            frame_move_write(frame, y0 + box_h, outer_x, "", outer_w, border_bg, true);
    }
    if (left_w > 0 || right_w > 0) {
        for (int r = 0; r < box_h; r++) {
            int y = y0 + r;
            if (y < 1 || y > rows)
                continue;
            if (left_w > 0)
                frame_move_write(frame, y, outer_x, "", left_w, border_bg, true);
            if (right_w > 0)
                frame_move_write(frame, y, x0 + box_w, "", right_w, border_bg, true);
        }
    }
}

typedef struct {
    char **lines;
    size_t count;
    size_t cap;
} guidance_lines_t;

typedef struct {
    const knox_widget_cmd_t *command;
    char keys[40];
    size_t first_key_len;
    bool custom_key;
} guidance_entry_t;

static int guidance_compare_command(const void *a, const void *b)
{
    const guidance_entry_t *left = a;
    const guidance_entry_t *right = b;
    return strcmp(left->command->name, right->command->name);
}

static int guidance_compare_key(const void *a, const void *b)
{
    const guidance_entry_t *left = a;
    const guidance_entry_t *right = b;
    size_t len = left->first_key_len < right->first_key_len
                 ? left->first_key_len : right->first_key_len;
    int order = strncasecmp(left->keys, right->keys, len);
    if (order != 0)
        return order;
    if (left->first_key_len != right->first_key_len)
        return left->first_key_len < right->first_key_len ? -1 : 1;
    return guidance_compare_command(a, b);
}

static void guidance_lines_push(guidance_lines_t *gl, const char *line)
{
    if (!gl || !line)
        return;
    if (gl->count >= gl->cap) {
        size_t ncap = gl->cap ? gl->cap * 2 : 64;
        char **nl = realloc(gl->lines, ncap * sizeof(*nl));
        if (!nl)
            return;
        gl->lines = nl;
        gl->cap = ncap;
    }
    char *copy = strdup(line);
    if (copy)
        gl->lines[gl->count++] = copy;
}

static void guidance_lines_free(guidance_lines_t *gl)
{
    if (!gl)
        return;
    for (size_t i = 0; i < gl->count; i++)
        free(gl->lines[i]);
    free(gl->lines);
    gl->lines = NULL;
    gl->count = 0;
    gl->cap = 0;
}

static void guidance_append_wrapped(guidance_lines_t *gl, int box_w,
                                    const char *keys, const char *name,
                                    const char *desc)
{
    const int desc_col = 42; /* "  %-16s %-22s " */
    const char *d = desc ? desc : "";

    if (box_w >= 58) {
        int desc_w = box_w - desc_col - 1;
        if (desc_w < 12)
            desc_w = 12;
        bool first = true;
        while (*d || first) {
            while (*d == ' ' && !first)
                d++;
            if (!*d && !first)
                break;
            size_t len = strlen(d);
            size_t take = len;
            if ((int)len > desc_w) {
                take = (size_t)desc_w;
                while (take > 0 && d[take] != ' ')
                    take--;
                if (take == 0)
                    take = (size_t)desc_w;
            }
            char line[512];
            if (first) {
                snprintf(line, sizeof(line), "  %-16s %-22s %.*s",
                         keys, name, (int)take, d);
                first = false;
            } else {
                snprintf(line, sizeof(line), "%*s%.*s",
                         desc_col, "", (int)take, d);
            }
            guidance_lines_push(gl, line);
            d += take;
            if (!*d)
                break;
        }
    } else {
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "  %s  (%s)", keys, name);
        guidance_lines_push(gl, hdr);
        int desc_w = box_w - 5;
        if (desc_w < 12)
            desc_w = 12;
        while (*d) {
            while (*d == ' ')
                d++;
            if (!*d)
                break;
            size_t len = strlen(d);
            size_t take = len;
            if ((int)len > desc_w) {
                take = (size_t)desc_w;
                while (take > 0 && d[take] != ' ')
                    take--;
                if (take == 0)
                    take = (size_t)desc_w;
            }
            char line[512];
            snprintf(line, sizeof(line), "    %.*s", (int)take, d);
            guidance_lines_push(gl, line);
            d += take;
        }
    }
}

static void render_modal_overlay(knox_app_t *app, tam_str_t *frame,
                                 int rows, int cols)
{
    if (app->modal == MODAL_NONE)
        return;

    if (app->modal == MODAL_SEARCH || app->modal == MODAL_COMMIT ||
        app->modal == MODAL_CONFIRM_SYNC ||
        app->modal == MODAL_BRANCH_CREATE ||
        app->modal == MODAL_STASH_PUSH ||
        app->modal == MODAL_CONFIRM_STASH_DROP) {
        const char *prefix =
            (app->modal == MODAL_SEARCH && app->focus == FOCUS_SIDEBAR_EXPLORER)
                                                ? "Filter Explorer (Esc clears): " :
            (app->modal == MODAL_SEARCH)        ? "Find: " :
            (app->modal == MODAL_COMMIT)        ? "Commit message (staged files): " :
            (app->modal == MODAL_BRANCH_CREATE) ? "Create & checkout new branch name: " :
            (app->modal == MODAL_STASH_PUSH)    ? "Stash message (optional, includes untracked): " :
            "Confirm: ";
        char bar[512];
        if (app->modal == MODAL_CONFIRM_SYNC ||
            app->modal == MODAL_CONFIRM_STASH_DROP)
            snprintf(bar, sizeof(bar), " %s", app->status_msg);
        else
            snprintf(bar, sizeof(bar), " %s%s_", prefix, app->prompt_buf);
        const char *bg = app->opts.color ? "\033[1;30;43m" : "\033[7m";
        frame_move_write(frame, rows - 1, 1, bar, cols, bg, true);
        return;
    }

    int pct80 = (cols * 4) / 5;
    int target_w = (pct80 > 74) ? pct80 : 74;
    int box_w = (cols - 6 < target_w) ? (cols - 6) : target_w;
    if (box_w < 30)
        box_w = cols;
    int box_h = rows - 4;
    if (box_h < 8)
        box_h = rows - 2;

    int x0 = (cols - box_w) / 2 + 1;
    int y0 = (rows - box_h) / 2 + 1;
    const char *bg = app->opts.color
                     ? (app->opts.linux_vt ? "\033[37;40m" : "\033[38;5;254;48;5;235m")
                     : "\033[7m";
    const char *hdr_bg = sgr_header_focused(app);

    render_modal_border(app, frame, rows, cols, x0, y0, box_w, box_h);

    if (app->modal == MODAL_GUIDANCE) {
        guidance_lines_t gl = {0};
        guidance_entry_t entries[WIDGET_COUNT];
        for (size_t i = 0; i < WIDGET_COUNT; i++) {
            guidance_entry_t *entry = &entries[i];
            entry->command = &WIDGET_TABLE[i];
            entry->custom_key = knox_keymap_find_widget_key(&app->keymap,
                entry->command->name, entry->keys, sizeof(entry->keys));
            if (!entry->custom_key)
                snprintf(entry->keys, sizeof(entry->keys), "%s", entry->command->keys);
            const char *separator = strstr(entry->keys, " / ");
            entry->first_key_len = separator ? (size_t)(separator - entry->keys)
                                             : strlen(entry->keys);
        }
        qsort(entries, WIDGET_COUNT, sizeof(entries[0]), app->guidance_sort_by_command
              ? guidance_compare_command : guidance_compare_key);
        for (size_t i = 0; i < WIDGET_COUNT; i++) {
            const guidance_entry_t *entry = &entries[i];
            char key_disp[sizeof(entry->keys) + 1];
            snprintf(key_disp, sizeof(key_disp), "%s%s", entry->keys,
                     entry->custom_key ? "*" : "");
            guidance_append_wrapped(&gl, box_w, key_disp,
                                    entry->command->name, entry->command->desc);
        }
        for (size_t i = 0; i < GUIDANCE_EXTRA_COUNT; i++) {
            guidance_append_wrapped(&gl, box_w,
                                    GUIDANCE_EXTRA_TABLE[i].keys,
                                    GUIDANCE_EXTRA_TABLE[i].name,
                                    GUIDANCE_EXTRA_TABLE[i].desc);
        }

        int header_rows = box_h > 2 ? 2 : 1;
        size_t list_rows = box_h > header_rows ? (size_t)(box_h - header_rows) : 0;
        size_t max_top = (gl.count > list_rows) ? (gl.count - list_rows) : 0;
        if (app->guidance_top > max_top)
            app->guidance_top = max_top;

        size_t first_row = gl.count ? (app->guidance_top + 1) : 0;
        size_t last_row = app->guidance_top + list_rows;
        if (last_row > gl.count)
            last_row = gl.count;

        char title[256];
        snprintf(title, sizeof(title),
                 " Keybinding Guidance [%s] %zu-%zu/%zu",
                 app->guidance_sort_by_command ? "Command" : "Key",
                 first_row, last_row, gl.count);
        frame_move_write(frame, y0, x0, title, box_w, hdr_bg, true);
        if (header_rows == 2)
            frame_move_write(frame, y0 + 1, x0,
                             " Tab/Left/Right sort | Up/Down scroll | g/Esc close",
                             box_w, hdr_bg, true);

        for (int r = header_rows; r < box_h; r++) {
            size_t idx = app->guidance_top + (size_t)(r - header_rows);
            const char *line = (idx < gl.count) ? gl.lines[idx] : "";
            frame_move_write(frame, y0 + r, x0, line, box_w, bg, true);
        }
        guidance_lines_free(&gl);
    } else if (app->modal == MODAL_PALETTE) {
        char mod_str[64] = "";
        if (app->chord_mods & TAM_MOD_CTRL)  strcat(mod_str, "Ctrl+");
        if (app->chord_mods & TAM_MOD_ALT)   strcat(mod_str, "Alt+");
        if (app->chord_mods & TAM_MOD_SHIFT) strcat(mod_str, "Shift+");

        char title[512];
        snprintf(title, sizeof(title),
                 " Command Palette & Chord Composer [Tab: latch modifier (%s)] | Filter: %s_",
                 mod_str[0] ? mod_str : "none", app->prompt_buf);
        frame_move_write(frame, y0, x0, title, box_w, hdr_bg, true);

        size_t match_indices[WIDGET_COUNT];
        size_t match_count = 0;
        for (size_t i = 0; i < WIDGET_COUNT; i++) {
            if (app->prompt_len == 0 ||
                strcasestr(WIDGET_TABLE[i].name, app->prompt_buf) ||
                strcasestr(WIDGET_TABLE[i].desc, app->prompt_buf) ||
                strcasestr(WIDGET_TABLE[i].keys, app->prompt_buf)) {
                match_indices[match_count++] = i;
            }
        }
        if (app->palette_cursor >= match_count && match_count > 0)
            app->palette_cursor = match_count - 1;

        for (int r = 1; r < box_h; r++) {
            size_t m = (size_t)(r - 1);
            char line[256] = "";
            const char *row_sgr = bg;
            if (m < match_count) {
                size_t wi = match_indices[m];
                char key_disp[40];
                char custom_key[32];
                if (knox_keymap_find_widget_key(&app->keymap, WIDGET_TABLE[wi].name,
                                                custom_key, sizeof(custom_key))) {
                    snprintf(key_disp, sizeof(key_disp), "%s*", custom_key);
                } else {
                    snprintf(key_disp, sizeof(key_disp), "%s", WIDGET_TABLE[wi].keys);
                }
                snprintf(line, sizeof(line), "  %-22s %-16s %s",
                         WIDGET_TABLE[wi].name,
                         key_disp,
                         WIDGET_TABLE[wi].desc);
                if (m == app->palette_cursor)
                    row_sgr = sgr_row_selected(app, true);
            }
            frame_move_write(frame, y0 + r, x0, line, box_w, row_sgr, true);
        }
    } else if (app->modal == MODAL_BRANCH) {
        const char *rname = (app->active_repo_idx < app->ws.count)
                            ? app->ws.repos[app->active_repo_idx].name : "-";
        char title[512];
        snprintf(title, sizeof(title),
                 " Branches: %s [Enter: Checkout | Alt+N: New Branch | Esc: Close] Filter: %s_",
                 rname, app->prompt_buf);
        frame_move_write(frame, y0, x0, title, box_w, hdr_bg, true);

        size_t match_count = 0;
        for (size_t i = 0; i < app->branch_count; i++) {
            const knox_branch_info_t *b = &app->branch_list[i];
            if (app->prompt_len == 0 ||
                (b->name && strcasestr(b->name, app->prompt_buf)) ||
                (b->subject && strcasestr(b->subject, app->prompt_buf))) {
                match_count++;
            }
        }
        if (app->branch_cursor >= match_count && match_count > 0)
            app->branch_cursor = match_count - 1;
        int list_rows = box_h - 1;
        if (app->branch_cursor < app->branch_top)
            app->branch_top = app->branch_cursor;
        if (list_rows > 0 && app->branch_cursor >= app->branch_top + (size_t)list_rows)
            app->branch_top = app->branch_cursor - (size_t)list_rows + 1;

        for (int r = 1; r < box_h; r++) {
            size_t vis_idx = app->branch_top + (size_t)(r - 1);
            char line[512] = "";
            const char *row_sgr = bg;

            if (match_count == 0 && r == 1) {
                snprintf(line, sizeof(line), "  (No matching branches; press Alt+N to create one)");
            } else if (vis_idx < match_count) {
                size_t m = 0;
                const knox_branch_info_t *b = NULL;
                for (size_t i = 0; i < app->branch_count; i++) {
                    const knox_branch_info_t *cand = &app->branch_list[i];
                    if (app->prompt_len == 0 ||
                        (cand->name && strcasestr(cand->name, app->prompt_buf)) ||
                        (cand->subject && strcasestr(cand->subject, app->prompt_buf))) {
                        if (m == vis_idx) {
                            b = cand;
                            break;
                        }
                        m++;
                    }
                }
                if (b) {
                    snprintf(line, sizeof(line), " %s %-24.24s %-8s %-8.8s %s",
                             b->is_current ? "[*]" : "   ",
                             b->name ? b->name : "",
                             b->is_remote ? "[remote]" : "[local]",
                             b->hash,
                             b->subject ? b->subject : "");
                    if (vis_idx == app->branch_cursor)
                        row_sgr = sgr_row_selected(app, true);
                }
            }
            frame_move_write(frame, y0 + r, x0, line, box_w, row_sgr, true);
        }
    } else if (app->modal == MODAL_STASH) {
        const char *rname = (app->active_repo_idx < app->ws.count)
                            ? app->ws.repos[app->active_repo_idx].name : "-";
        char title[512];
        snprintf(title, sizeof(title),
                 " Stashes: %s (%zu) [Enter: Diff | n/Alt+N: Push | a: Apply | p: Pop | d: Drop]",
                 rname, app->stash_count);
        frame_move_write(frame, y0, x0, title, box_w, hdr_bg, true);

        if (app->stash_cursor >= app->stash_count && app->stash_count > 0)
            app->stash_cursor = app->stash_count - 1;
        int list_rows = box_h - 1;
        if (app->stash_cursor < app->stash_top)
            app->stash_top = app->stash_cursor;
        if (list_rows > 0 && app->stash_cursor >= app->stash_top + (size_t)list_rows)
            app->stash_top = app->stash_cursor - (size_t)list_rows + 1;

        for (int r = 1; r < box_h; r++) {
            size_t idx = app->stash_top + (size_t)(r - 1);
            char line[512] = "";
            const char *row_sgr = bg;
            if (app->stash_count == 0 && r == 1) {
                snprintf(line, sizeof(line),
                         "  (No stashes in %s; press n or Alt+N to stash working changes)",
                         rname);
            } else if (idx < app->stash_count) {
                const knox_stash_entry_t *st = &app->stash_list[idx];
                char dstr[32];
                format_commit_date(st->timestamp, dstr, sizeof(dstr));
                snprintf(line, sizeof(line), "  %-10s [%-12.12s] %-16s %s",
                         st->ref ? st->ref : "stash",
                         (st->branch && *st->branch) ? st->branch : "-",
                         dstr,
                         st->message ? st->message : "");
                if (idx == app->stash_cursor)
                    row_sgr = sgr_row_selected(app, true);
            }
            frame_move_write(frame, y0 + r, x0, line, box_w, row_sgr, true);
        }
    }
}

static void render_footer(knox_app_t *app, tam_str_t *frame,
                          int rows, int cols)
{
    const char *zone_name =
        (app->focus == FOCUS_SIDEBAR_REPOS)    ? "Repos" :
        (app->focus == FOCUS_SIDEBAR_CHANGES)  ? "Changes" :
        (app->focus == FOCUS_SIDEBAR_EXPLORER) ? "Explorer" : "Workbench";

    const char *hints =
        (app->focus == FOCUS_SIDEBAR_REPOS)
        ? "[Enter/Right] Explore  [Alt+3] Graph  [f] Fetch  [s/Alt+S] Sync  [c/Alt+K] Branches  [z/Alt+Z] Stash  [g] Keys"
        : (app->focus == FOCUS_SIDEBAR_CHANGES)
        ? "[Space] Stage  [Enter] Diff  [Alt+C] Commit  [z/Alt+Z] Stash  [Alt+E] Edit  [g] Keys"
        : (app->focus == FOCUS_SIDEBAR_EXPLORER)
        ? "[Enter/Right] Open  [Left] Collapse  [Bksp/Alt+1] Repos  [/] Filter  [g] Keys"
        : "[Enter] Inspect  [d/R] Diff/Render  [c/Alt+K] Branches  [Alt+V/H] Split  [g] Keys";

    char left_label[512];
    if (app->status_msg[0]) {
        snprintf(left_label, sizeof(left_label), " [%s] %s | %s",
                 zone_name, app->status_msg, hints);
    } else {
        snprintf(left_label, sizeof(left_label), " [%s] %s",
                 zone_name, hints);
    }

    size_t attn = 1, top = 1, bot = 1, total = 1;
    knox_pane_t *ap = app_active_pane(app);
    if (ap && ap->content == KNOX_PANE_GRAPH && app->graph.commit_count > 0) {
        attn = ap->graph_cursor + 1;
        top = ap->graph_top + 1;
        total = app->graph.commit_count;
        bot = top + (size_t)(ap->h > 2 ? ap->h - 2 : 1) - 1;
        if (bot > total) bot = total;
    } else if (ap && ap->content == KNOX_PANE_VIEWER) {
        total = (ap->doc.kind == KNOX_DOC_DIFF && ap->doc.side_by_side)
                ? ap->doc.sbs_count
                : (ap->doc.render_mode ? ap->doc.rend_line_count : ap->doc.line_count);
        if (total == 0) total = 1;
        attn = ap->doc.cursor_line + 1;
        top = ap->doc.top_line + 1;
        bot = top + (size_t)(ap->h > 1 ? ap->h - 1 : 1) - 1;
        if (bot > total) bot = total;
    }

    tam_pager_footer_t pf = {
        .label = left_label,
        .has_attn = true,
        .attn_line = attn,
        .top_line = top,
        .bot_line = bot,
        .total_lines = total,
        .total_known = true,
        .link_hint = NULL,
        .version = KNOX_VERSION
    };
    char footer_buf[1024];
    tam_pager_format_footer(footer_buf, sizeof(footer_buf), cols, &pf);

    const char *bg = (app->opts.color && !app->opts.linux_vt)
                     ? "\033[38;5;254;48;5;236m" : "\033[7m";
    frame_move_write(frame, rows, 1, footer_buf, cols,
                     app->opts.color ? bg : "", true);
}

static void app_draw(knox_app_t *app)
{
    int rows = app->tty.rows;
    int cols = app->tty.cols;
    if (rows < 6) rows = 24;
    if (cols < 40) cols = 80;

    app->sidebar_width = cols / 3;
    if (app->sidebar_width < 32)
        app->sidebar_width = 32;
    if (app->sidebar_width > 48)
        app->sidebar_width = 48;
    if (cols < 68)
        app->sidebar_width = cols / 2;

    int body_y = 2;
    int body_h = rows - 2;
    int main_x = 1;
    int main_w = cols;

    tam_str_t frame = tam_str_new("");
    render_top_bar(app, &frame, cols);

    if (app->sidebar_visible) {
        render_sidebar(app, &frame, 1, body_y, app->sidebar_width, body_h);
        const struct tam_render_box *bx = tam_render_box_chars(app->opts.ascii);
        int sep_x = app->sidebar_width + 1;
        for (int r = 0; r < body_h; r++) {
            frame_move_write(&frame, body_y + r, sep_x, bx->v, 1,
                             app->opts.color ? "\033[90m" : "", false);
        }
        main_x = app->sidebar_width + 2;
        main_w = cols - app->sidebar_width - 1;
    }

    knox_tab_t *tab = app_current_tab(app);
    if (tab && tab->root) {
        knox_split_layout(tab->root, main_x, body_y, main_w, body_h);
        render_split_tree(app, tab->root, &frame);
    }

    render_footer(app, &frame, rows, cols);
    render_modal_overlay(app, &frame, rows, cols);

    tam_tty_frame_begin(&app->tty);
    tam_tty_write(&app->tty, frame, tam_str_len(frame));
    tam_tty_frame_end(&app->tty);
    tam_str_free(frame);
}

/* ========================================================================= */
/* Widget Dispatch & Keyboard Event Handling                                 */
/* ========================================================================= */

static void app_move_selection(knox_app_t *app, int delta)
{
    if (app->focus == FOCUS_SIDEBAR_REPOS) {
        if (app->ws.count == 0)
            return;
        ssize_t next = (ssize_t)app->active_repo_idx + delta;
        if (next < 0) next = 0;
        if ((size_t)next >= app->ws.count) next = (ssize_t)app->ws.count - 1;
        if ((size_t)next != app->active_repo_idx) {
            app->active_repo_idx = (size_t)next;
            app->change_cursor = 0;
            app->change_top = 0;
            /* Sync Git Graph pane in active tab if visible */
            knox_pane_t *ap = app_active_pane(app);
            if (ap && ap->content == KNOX_PANE_GRAPH) {
                ap->repo_idx = app->active_repo_idx;
                ap->graph_cursor = 0;
                ap->graph_top = 0;
                app_ensure_repo_graph(app, app->active_repo_idx,
                                      ap->graph_all_branches,
                                      ap->graph_show_remotes, true);
            }
        }
    } else if (app->focus == FOCUS_SIDEBAR_CHANGES) {
        if (app->active_repo_idx >= app->ws.count)
            return;
        size_t cnt = app->ws.repos[app->active_repo_idx].change_count;
        if (cnt == 0)
            return;
        ssize_t next = (ssize_t)app->change_cursor + delta;
        if (next < 0) next = 0;
        if ((size_t)next >= cnt) next = (ssize_t)cnt - 1;
        app->change_cursor = (size_t)next;
    } else if (app->focus == FOCUS_SIDEBAR_EXPLORER) {
        if (app->explorer.flat_count == 0)
            return;
        ssize_t next = (ssize_t)app->explorer_cursor + delta;
        if (next < 0) next = 0;
        if ((size_t)next >= app->explorer.flat_count)
            next = (ssize_t)app->explorer.flat_count - 1;
        app->explorer_cursor = (size_t)next;
    } else if (app->focus == FOCUS_MAIN_PANE) {
        knox_pane_t *ap = app_active_pane(app);
        if (!ap)
            return;
        if (ap->content == KNOX_PANE_GRAPH) {
            if (app->graph.commit_count == 0)
                return;
            ssize_t next = (ssize_t)ap->graph_cursor + delta;
            if (next < 0) next = 0;
            if ((size_t)next >= app->graph.commit_count)
                next = (ssize_t)app->graph.commit_count - 1;
            ap->graph_cursor = (size_t)next;
        } else if (ap->content == KNOX_PANE_COMMIT_DETAIL) {
            if (app->details.file_count == 0)
                return;
            ssize_t next = (ssize_t)ap->inspect_file_cursor + delta;
            if (next < 0) next = 0;
            if ((size_t)next >= app->details.file_count)
                next = (ssize_t)app->details.file_count - 1;
            ap->inspect_file_cursor = (size_t)next;
        } else if (ap->content == KNOX_PANE_VIEWER) {
            size_t total = (ap->doc.kind == KNOX_DOC_DIFF && ap->doc.side_by_side)
                           ? ap->doc.sbs_count
                           : (ap->doc.render_mode ? ap->doc.rend_line_count : ap->doc.line_count);
            if (total == 0)
                return;
            ssize_t next = (ssize_t)ap->doc.cursor_line + delta;
            if (next < 0) next = 0;
            if ((size_t)next >= total) next = (ssize_t)total - 1;
            ap->doc.cursor_line = (size_t)next;
        }
    }
}

static void fetch_all_progress_cb(size_t idx, size_t total,
                                  const knox_repo_t *repo, void *user)
{
    knox_app_t *app = (knox_app_t *)user;
    if (!app || !repo)
        return;
    app_set_status(app, "Fetching [%zu/%zu] %s...", idx + 1, total, repo->name);
    app_draw(app);
}

static void app_run_repo_fetch(knox_app_t *app)
{
    if (app->active_repo_idx >= app->ws.count)
        return;
    knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
    app_set_status(app, "Fetching %s (%s)...",
                   repo->name, repo->branch ? repo->branch : "?");
    app_draw(app);

    char summary[256] = "";
    int rc = knox_repo_fetch(repo, summary, sizeof(summary));
    app_set_status(app, "%s", summary);
    if (rc == 0) {
        bool all_branches = true;
        bool show_remotes = true;
        knox_pane_t *ap = app_active_pane(app);
        if (ap && ap->content == KNOX_PANE_GRAPH &&
            ap->repo_idx == app->active_repo_idx) {
            all_branches = ap->graph_all_branches;
            show_remotes = ap->graph_show_remotes;
        }
        app_ensure_repo_graph(app, app->active_repo_idx,
                              all_branches, show_remotes, true);
        knox_explorer_refresh_git_status(&app->explorer, &app->ws);
    }
}

static void app_run_repo_sync(knox_app_t *app, bool allow_protected_push)
{
    if (app->active_repo_idx >= app->ws.count)
        return;
    knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
    bool needs_confirm_now = (!allow_protected_push && repo->ahead > 0 &&
                              knox_branch_needs_push_confirm(repo->branch));
    if (!needs_confirm_now) {
        app_set_status(app, "Syncing %s (%s)...",
                       repo->name, repo->branch ? repo->branch : "?");
        app_draw(app);
    }

    char summary[256] = "";
    int rc = knox_repo_sync(repo, allow_protected_push, summary, sizeof(summary));
    app_set_status(app, "%s", summary);
    if (rc == 1) {
        app->modal = MODAL_CONFIRM_SYNC;
    } else {
        bool all_branches = true;
        bool show_remotes = true;
        knox_pane_t *ap = app_active_pane(app);
        if (ap && ap->content == KNOX_PANE_GRAPH &&
            ap->repo_idx == app->active_repo_idx) {
            all_branches = ap->graph_all_branches;
            show_remotes = ap->graph_show_remotes;
        }
        app_ensure_repo_graph(app, app->active_repo_idx,
                              all_branches, show_remotes, true);
        knox_explorer_refresh_git_status(&app->explorer, &app->ws);
    }
}

static void app_execute_widget(knox_app_t *app, const char *widget)
{
    if (!widget)
        return;

    if (strcmp(widget, "quit") == 0) {
        app->running = false;
    } else if (strcmp(widget, "guidance") == 0) {
        if (app->modal == MODAL_GUIDANCE) {
            app->modal = MODAL_NONE;
        } else {
            app->modal = MODAL_GUIDANCE;
            app->guidance_top = 0;
        }
    } else if (strcmp(widget, "execute") == 0) {
        app->modal = MODAL_PALETTE;
        app->prompt_buf[0] = '\0';
        app->prompt_len = 0;
        app->palette_cursor = 0;
        app->chord_mods = 0;
    } else if (strcmp(widget, "view-source-control") == 0) {
        if (app->focus == FOCUS_SIDEBAR_EXPLORER &&
            app->explorer_cursor < app->explorer.flat_count) {
            const knox_tree_node_t *n = app->explorer.flat[app->explorer_cursor];
            if (n && n->abs_path) {
                knox_repo_t *r = knox_workspace_find_repo_for_path(&app->ws, n->abs_path);
                if (r) {
                    size_t idx = (size_t)(r - app->ws.repos);
                    if (idx != app->active_repo_idx) {
                        app->active_repo_idx = idx;
                        app->change_cursor = 0;
                        app->change_top = 0;
                    }
                }
            }
        }
        app->sidebar_visible = true;
        app->sidebar_mode = SIDEBAR_SOURCE_CONTROL;
        app->focus = FOCUS_SIDEBAR_REPOS;
    } else if (strcmp(widget, "view-explorer") == 0) {
        app->sidebar_visible = true;
        app->sidebar_mode = SIDEBAR_EXPLORER;
        app->focus = FOCUS_SIDEBAR_EXPLORER;
    } else if (strcmp(widget, "explore-repo") == 0) {
        app->sidebar_visible = true;
        app->sidebar_mode = SIDEBAR_EXPLORER;
        app->focus = FOCUS_SIDEBAR_EXPLORER;
        if (app->active_repo_idx < app->ws.count) {
            const knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
            size_t flat_idx = 0;
            if (knox_explorer_reveal_path(&app->explorer, repo->root_path,
                                          &app->ws, &flat_idx) == 0) {
                app->explorer_cursor = flat_idx;
                app->explorer_top = flat_idx;
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Exploring %s", repo->name);
            }
        }
    } else if (strcmp(widget, "view-graph") == 0) {
        for (size_t i = 0; i < app->tab_count; i++) {
            if (app->tabs[i].active_pane &&
                app->tabs[i].active_pane->content == KNOX_PANE_GRAPH) {
                knox_pane_t *gp = app->tabs[i].active_pane;
                app->active_tab = i;
                app->focus = FOCUS_MAIN_PANE;
                if (app->active_repo_idx < app->ws.count &&
                    gp->repo_idx != app->active_repo_idx) {
                    gp->repo_idx = app->active_repo_idx;
                    gp->graph_cursor = 0;
                    gp->graph_top = 0;
                    app_ensure_repo_graph(app, app->active_repo_idx,
                                          gp->graph_all_branches,
                                          gp->graph_show_remotes, true);
                }
                return;
            }
        }
        knox_pane_t *gp = knox_pane_new(KNOX_PANE_GRAPH, app->active_repo_idx);
        if (gp) {
            app_add_tab(app, "Git Graph", gp);
            app->focus = FOCUS_MAIN_PANE;
        }
    } else if (strcmp(widget, "toggle-sidebar") == 0) {
        app->sidebar_visible = !app->sidebar_visible;
        if (!app->sidebar_visible)
            app->focus = FOCUS_MAIN_PANE;
    } else if (strcmp(widget, "focus-next") == 0) {
        if (!app->sidebar_visible) {
            knox_tab_t *t = app_current_tab(app);
            if (t && t->root)
                t->active_pane = knox_split_next_pane(t->root, t->active_pane);
            app->focus = FOCUS_MAIN_PANE;
        } else if (app->sidebar_mode == SIDEBAR_SOURCE_CONTROL) {
            if (app->focus == FOCUS_SIDEBAR_REPOS)
                app->focus = FOCUS_SIDEBAR_CHANGES;
            else if (app->focus == FOCUS_SIDEBAR_CHANGES)
                app->focus = FOCUS_MAIN_PANE;
            else
                app->focus = FOCUS_SIDEBAR_REPOS;
        } else {
            app->focus = (app->focus == FOCUS_SIDEBAR_EXPLORER)
                         ? FOCUS_MAIN_PANE : FOCUS_SIDEBAR_EXPLORER;
        }
    } else if (strcmp(widget, "focus-prev") == 0) {
        if (!app->sidebar_visible) {
            app->focus = FOCUS_MAIN_PANE;
        } else if (app->sidebar_mode == SIDEBAR_SOURCE_CONTROL) {
            if (app->focus == FOCUS_MAIN_PANE)
                app->focus = FOCUS_SIDEBAR_CHANGES;
            else if (app->focus == FOCUS_SIDEBAR_CHANGES)
                app->focus = FOCUS_SIDEBAR_REPOS;
            else
                app->focus = FOCUS_MAIN_PANE;
        } else {
            app->focus = (app->focus == FOCUS_MAIN_PANE)
                         ? FOCUS_SIDEBAR_EXPLORER : FOCUS_MAIN_PANE;
        }
    } else if (strcmp(widget, "split-vertical") == 0 ||
               strcmp(widget, "split-horizontal") == 0) {
        knox_tab_t *t = app_current_tab(app);
        if (!t || !t->active_pane)
            return;
        knox_split_node_t *leaf = knox_split_find_pane(t->root, t->active_pane);
        if (!leaf)
            return;
        knox_pane_t *np = knox_pane_new(t->active_pane->content,
                                        app->active_repo_idx);
        if (!np)
            return;
        if (np->content == KNOX_PANE_VIEWER && t->active_pane->doc.abs_path) {
            knox_viewer_open_file(&np->doc, t->active_pane->doc.abs_path,
                                  t->active_pane->doc.repo_name,
                                  t->active_pane->doc.render_mode,
                                  t->active_pane->w / 2, app->opts.ascii);
        } else if (np->content == KNOX_PANE_VIEWER && t->active_pane->doc.raw_text) {
            knox_viewer_open_buffer(&np->doc, t->active_pane->doc.kind,
                                    t->active_pane->doc.title,
                                    t->active_pane->doc.repo_name,
                                    t->active_pane->doc.breadcrumb,
                                    t->active_pane->doc.raw_text,
                                    t->active_pane->doc.raw_len,
                                    t->active_pane->doc.lang);
        }
        knox_node_type_t st = (strcmp(widget, "split-vertical") == 0)
                              ? KNOX_NODE_SPLIT_V : KNOX_NODE_SPLIT_H;
        if (knox_split_pane(leaf, st, np)) {
            t->active_pane = np;
            app->focus = FOCUS_MAIN_PANE;
            app_set_status(app, "Split pane %s",
                           st == KNOX_NODE_SPLIT_V ? "vertically" : "horizontally");
        } else {
            knox_pane_free(np);
        }
    } else if (strcmp(widget, "focus-other-pane") == 0) {
        knox_tab_t *t = app_current_tab(app);
        if (t && t->root) {
            t->active_pane = knox_split_next_pane(t->root, t->active_pane);
            app->focus = FOCUS_MAIN_PANE;
        }
    } else if (strcmp(widget, "tab-prev") == 0) {
        if (app->tab_count > 1) {
            app->active_tab = (app->active_tab + app->tab_count - 1) % app->tab_count;
            app->focus = FOCUS_MAIN_PANE;
        }
    } else if (strcmp(widget, "tab-next") == 0) {
        if (app->tab_count > 1) {
            app->active_tab = (app->active_tab + 1) % app->tab_count;
            app->focus = FOCUS_MAIN_PANE;
        }
    } else if (strncmp(widget, "tab-", 4) == 0 &&
               widget[4] >= '0' && widget[4] <= '9' && widget[5] == '\0') {
        size_t idx = (widget[4] == '0') ? 9 : (size_t)(widget[4] - '1');
        if (idx < app->tab_count) {
            app->active_tab = idx;
            app->focus = FOCUS_MAIN_PANE;
        }
    } else if (strcmp(widget, "new") == 0) {
        knox_pane_t *gp = knox_pane_new(KNOX_PANE_GRAPH, app->active_repo_idx);
        if (gp) {
            char title[64];
            const char *rname = (app->active_repo_idx < app->ws.count)
                                ? app->ws.repos[app->active_repo_idx].name
                                : "Repo";
            snprintf(title, sizeof(title), "Graph %s", rname);
            app_add_tab(app, title, gp);
            app->focus = FOCUS_MAIN_PANE;
        }
    } else if (strcmp(widget, "close") == 0) {
        knox_tab_t *t = app_current_tab(app);
        if (!t || !t->root)
            return;
        if (knox_split_count_leaves(t->root) > 1 && t->active_pane) {
            knox_split_node_t *leaf = knox_split_find_pane(t->root, t->active_pane);
            knox_split_node_t *new_root = t->root;
            knox_pane_t *next_p = knox_split_close(t->root, leaf, &new_root);
            t->root = new_root;
            if (next_p)
                t->active_pane = next_p;
            app_set_status(app, "Closed split pane");
        } else if (app->tab_count > 1) {
            knox_split_free_tree(t->root);
            for (size_t i = app->active_tab; i + 1 < app->tab_count; i++)
                app->tabs[i] = app->tabs[i + 1];
            app->tab_count--;
            if (app->active_tab >= app->tab_count)
                app->active_tab = app->tab_count - 1;
            app_set_status(app, "Closed tab");
        } else {
            app_set_status(app, "Cannot close the last open tab (press Ctrl+Q to quit)");
        }
    } else if (strcmp(widget, "toggle-render-mode") == 0) {
        knox_pane_t *ap = app_active_pane(app);
        if (ap && ap->content == KNOX_PANE_VIEWER) {
            knox_viewer_toggle_render(&ap->doc, ap->w > 4 ? ap->w - 2 : 72,
                                      app->opts.ascii);
            if (ap->doc.kind == KNOX_DOC_DIFF) {
                app_set_status(app, "Switched diff to %s mode",
                               ap->doc.side_by_side ? "Side-by-Side Split" : "Unified");
            } else {
                app_set_status(app, "Switched to %s mode",
                               ap->doc.render_mode ? "Rendered" : "Raw Syntax");
            }
        }
    } else if (strcmp(widget, "toggle-diff-split") == 0) {
        knox_pane_t *ap = app_active_pane(app);
        if (ap && ap->content == KNOX_PANE_VIEWER && ap->doc.kind == KNOX_DOC_DIFF) {
            knox_viewer_toggle_side_by_side(&ap->doc);
            app_set_status(app, "Switched diff to %s mode",
                           ap->doc.side_by_side ? "Side-by-Side Split" : "Unified");
        } else {
            app_set_status(app, "Open a file diff first (Enter on a changed file) to toggle Side-by-Side Diff");
        }
    } else if (strcmp(widget, "branch-modal") == 0) {
        if (app->active_repo_idx >= app->ws.count)
            return;
        knox_branch_list_free(app->branch_list, app->branch_count);
        app->branch_list = NULL;
        app->branch_count = 0;
        knox_repo_list_branches(&app->ws.repos[app->active_repo_idx],
                                &app->branch_list, &app->branch_count);
        app->branch_cursor = 0;
        app->branch_top = 0;
        app->prompt_buf[0] = '\0';
        app->prompt_len = 0;
        app->modal = MODAL_BRANCH;
    } else if (strcmp(widget, "stash-modal") == 0) {
        if (app->active_repo_idx >= app->ws.count)
            return;
        knox_stash_list_free(app->stash_list, app->stash_count);
        app->stash_list = NULL;
        app->stash_count = 0;
        knox_repo_list_stashes(&app->ws.repos[app->active_repo_idx],
                               &app->stash_list, &app->stash_count);
        app->stash_cursor = 0;
        app->stash_top = 0;
        app->prompt_buf[0] = '\0';
        app->prompt_len = 0;
        app->modal = MODAL_STASH;
    } else if (strcmp(widget, "cycle-branch-filter") == 0) {
        knox_pane_t *ap = app_active_pane(app);
        if (ap && ap->content == KNOX_PANE_GRAPH) {
            ap->graph_all_branches = !ap->graph_all_branches;
            app_ensure_repo_graph(app, ap->repo_idx, ap->graph_all_branches,
                                  ap->graph_show_remotes, true);
            app_set_status(app, "Git Graph branches: %s",
                           ap->graph_all_branches ? "Show All" : "Current (HEAD)");
        }
    } else if (strcmp(widget, "repo-fetch") == 0) {
        app_run_repo_fetch(app);
    } else if (strcmp(widget, "repo-sync") == 0) {
        app_run_repo_sync(app, false);
    } else if (strcmp(widget, "fetch-all") == 0) {
        knox_batch_fetch_result_t bres = {0};
        char summary[256] = "";
        knox_workspace_fetch_all(&app->ws, fetch_all_progress_cb, app,
                                 &bres, summary, sizeof(summary));
        if (app->active_repo_idx < app->ws.count)
            app_ensure_repo_graph(app, app->active_repo_idx, true, true, true);
        knox_explorer_refresh_git_status(&app->explorer, &app->ws);
        app_set_status(app, "%s", summary);
    } else if (strcmp(widget, "commit-prompt") == 0) {
        if (app->active_repo_idx >= app->ws.count)
            return;
        knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
        if (repo->staged_count == 0) {
            app_set_status(app, "%s: no staged files (press Space on a file in Changes to stage)",
                           repo->name);
            return;
        }
        app->modal = MODAL_COMMIT;
        app->prompt_buf[0] = '\0';
        app->prompt_len = 0;
    } else if (strcmp(widget, "find") == 0) {
        app->modal = MODAL_SEARCH;
        if (app->focus == FOCUS_SIDEBAR_EXPLORER && app->explorer.filter[0] != '\0') {
            snprintf(app->prompt_buf, sizeof(app->prompt_buf), "%s",
                     app->explorer.filter);
            app->prompt_len = strlen(app->prompt_buf);
        } else {
            app->prompt_buf[0] = '\0';
            app->prompt_len = 0;
        }
    } else if (strcmp(widget, "move-up") == 0) {
        app_move_selection(app, -1);
    } else if (strcmp(widget, "move-down") == 0) {
        app_move_selection(app, 1);
    } else if (strcmp(widget, "page-up") == 0) {
        app_move_selection(app, -12);
    } else if (strcmp(widget, "page-down") == 0) {
        app_move_selection(app, 12);
    } else if (strcmp(widget, "jump-top") == 0) {
        app_move_selection(app, -100000);
    } else if (strcmp(widget, "jump-bottom") == 0) {
        app_move_selection(app, 100000);
    } else if (strcmp(widget, "scroll-left") == 0) {
        if (app->focus == FOCUS_MAIN_PANE) {
            knox_pane_t *ap = app_active_pane(app);
            if (ap && ap->content == KNOX_PANE_VIEWER && !ap->doc.render_mode)
                ap->doc.left_col = (ap->doc.left_col >= 4) ? ap->doc.left_col - 4 : 0;
        }
    } else if (strcmp(widget, "scroll-right") == 0) {
        if (app->focus == FOCUS_MAIN_PANE) {
            knox_pane_t *ap = app_active_pane(app);
            if (ap && ap->content == KNOX_PANE_VIEWER && !ap->doc.render_mode)
                ap->doc.left_col += 4;
        }
    } else if (strcmp(widget, "clear-screen") == 0) {
        /* Redraws cleanly at end of event loop */
    } else if (strcmp(widget, "cancel-mode") == 0) {
        if (app->focus == FOCUS_SIDEBAR_EXPLORER && app->explorer.filter[0] != '\0') {
            knox_explorer_clear_filter(&app->explorer);
            app->explorer_cursor = 0;
            app->explorer_top = 0;
            app_set_status(app, "Cleared Explorer filter");
        } else {
            app->status_msg[0] = '\0';
        }
    } else if (strcmp(widget, "find-next") == 0 ||
               strcmp(widget, "find-prev") == 0) {
        bool fwd = (strcmp(widget, "find-next") == 0);
        knox_pane_t *ap = app_active_pane(app);
        if (ap && ap->content == KNOX_PANE_VIEWER)
            knox_viewer_search(&ap->doc, NULL, fwd, true);
    } else if (strcmp(widget, "update") == 0) {
        app_update(app);
    } else if (strcmp(widget, "open-in-editor") == 0) {
        app_open_in_external_editor(app);
    } else if (strcmp(widget, "copy") == 0) {
        knox_pane_t *ap = app_active_pane(app);
        if (app->focus == FOCUS_MAIN_PANE && ap) {
            if (ap->content == KNOX_PANE_GRAPH &&
                ap->graph_cursor < app->graph.commit_count) {
                app_copy_to_clipboard(app, app->graph.commits[ap->graph_cursor].hash);
            } else if (ap->content == KNOX_PANE_VIEWER && ap->doc.abs_path) {
                app_copy_to_clipboard(app, ap->doc.abs_path);
            }
        } else if (app->focus == FOCUS_SIDEBAR_REPOS &&
                   app->active_repo_idx < app->ws.count) {
            app_copy_to_clipboard(app, app->ws.repos[app->active_repo_idx].root_path);
        }
    } else if (strcmp(widget, "stage-or-page-down") == 0) {
        if (app->focus == FOCUS_SIDEBAR_CHANGES &&
            app->active_repo_idx < app->ws.count) {
            knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
            if (app->change_cursor < repo->change_count) {
                knox_file_change_t *fc = &repo->changes[app->change_cursor];
                char err[256] = "";
                char *path_copy = strdup(fc->path);
                bool was_staged = fc->staged && !fc->unstaged;
                int rc = was_staged
                         ? knox_repo_unstage_file(repo, path_copy, err, sizeof(err))
                         : knox_repo_stage_file(repo, path_copy, err, sizeof(err));
                if (rc == 0) {
                    app_set_status(app, "%s %s in %s",
                                   was_staged ? "Unstaged" : "Staged",
                                   path_copy, repo->name);
                    knox_explorer_refresh_git_status(&app->explorer, &app->ws);
                } else {
                    app_set_status(app, "Error: %s", err);
                }
                free(path_copy);
            }
        } else {
            knox_pane_t *ap = app_active_pane(app);
            int step = (ap && ap->h > 3) ? ap->h - 2 : 10;
            app_move_selection(app, step);
        }
    } else if (strcmp(widget, "open-or-inspect") == 0) {
        if (app->focus == FOCUS_SIDEBAR_REPOS) {
            if (app->active_repo_idx < app->ws.count)
                app_execute_widget(app, "explore-repo");
        } else if (app->focus == FOCUS_SIDEBAR_CHANGES) {
            if (app->active_repo_idx < app->ws.count) {
                const knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
                if (app->change_cursor < repo->change_count) {
                    const knox_file_change_t *fc = &repo->changes[app->change_cursor];
                    char *diff = knox_repo_diff_working_file(repo, fc);
                    char title[64], bc[256];
                    snprintf(title, sizeof(title), "Diff %s", fc->path);
                    snprintf(bc, sizeof(bc), "%s > %s (Working Tree Diff)",
                             repo->name, fc->path);
                    app_open_diff_in_pane_or_tab(app, title, repo->name, bc, diff);
                }
            }
        } else if (app->focus == FOCUS_SIDEBAR_EXPLORER) {
            if (app->explorer_cursor < app->explorer.flat_count) {
                knox_tree_node_t *n = app->explorer.flat[app->explorer_cursor];
                if (n->is_dir)
                    knox_explorer_toggle_node(&app->explorer, app->explorer_cursor, &app->ws);
                else
                    app_open_file_in_tab(app, n->abs_path);
            }
        } else if (app->focus == FOCUS_MAIN_PANE) {
            knox_pane_t *ap = app_active_pane(app);
            if (!ap)
                return;
            if (ap->content == KNOX_PANE_GRAPH) {
                app_inspect_commit_in_split(app, ap);
            } else if (ap->content == KNOX_PANE_COMMIT_DETAIL) {
                if (ap->repo_idx < app->ws.count &&
                    ap->inspect_file_cursor < app->details.file_count) {
                    const knox_repo_t *repo = &app->ws.repos[ap->repo_idx];
                    const char *rel = app->details.files[ap->inspect_file_cursor].path;
                    char *diff = knox_repo_diff_commit_file(repo, app->details.hash, rel);
                    char title[64], bc[256];
                    snprintf(title, sizeof(title), "%.7s:%s", app->details.hash, rel);
                    snprintf(bc, sizeof(bc), "%s > commit %.8s > %s",
                             repo->name, app->details.hash, rel);
                    app_open_diff_in_pane_or_tab(app, title, repo->name, bc, diff);
                }
            }
        }
    }
}

static void handle_modal_key(knox_app_t *app, const tam_key_event_t *ev)
{
    if (ev->type == TAM_KEY_ESC ||
        (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_CTRL) &&
         (ev->codepoint == '\\' || ev->codepoint == 'g' || ev->codepoint == 'G'))) {
        if (app->modal == MODAL_SEARCH && app->focus == FOCUS_SIDEBAR_EXPLORER) {
            knox_explorer_clear_filter(&app->explorer);
            app->explorer_cursor = 0;
            app->explorer_top = 0;
            app_set_status(app, "Cleared Explorer filter");
        }
        app->modal = MODAL_NONE;
        return;
    }

    if (app->modal == MODAL_CONFIRM_SYNC) {
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'y' || ev->codepoint == 'Y')) {
            app->modal = MODAL_NONE;
            app_run_repo_sync(app, true);
        } else {
            app->modal = MODAL_NONE;
            app_set_status(app, "Cancelled protected branch push");
        }
        return;
    }

    if (app->modal == MODAL_CONFIRM_STASH_DROP) {
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'y' || ev->codepoint == 'Y')) {
            if (app->active_repo_idx < app->ws.count && app->pending_stash_ref[0]) {
                char summary[256] = "";
                knox_repo_stash_drop(&app->ws.repos[app->active_repo_idx],
                                     app->pending_stash_ref, summary, sizeof(summary));
                app_set_status(app, "%s", summary);
            }
            app->pending_stash_ref[0] = '\0';
            app_execute_widget(app, "stash-modal");
        } else {
            app->pending_stash_ref[0] = '\0';
            app->modal = MODAL_STASH;
            app_set_status(app, "Cancelled stash drop");
        }
        return;
    }

    if (app->modal == MODAL_BRANCH) {
        if (ev->type == TAM_KEY_UP) {
            if (app->branch_cursor > 0)
                app->branch_cursor--;
            return;
        }
        if (ev->type == TAM_KEY_DOWN) {
            app->branch_cursor++;
            return;
        }
        if (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_ALT) &&
            (ev->codepoint == 'n' || ev->codepoint == 'N')) {
            app->modal = MODAL_BRANCH_CREATE;
            app->prompt_buf[0] = '\0';
            app->prompt_len = 0;
            return;
        }
        if (ev->type == TAM_KEY_ENTER) {
            size_t m = 0;
            const knox_branch_info_t *chosen = NULL;
            for (size_t i = 0; i < app->branch_count; i++) {
                const knox_branch_info_t *b = &app->branch_list[i];
                if (app->prompt_len == 0 ||
                    (b->name && strcasestr(b->name, app->prompt_buf)) ||
                    (b->subject && strcasestr(b->subject, app->prompt_buf))) {
                    if (m == app->branch_cursor) {
                        chosen = b;
                        break;
                    }
                    m++;
                }
            }
            app->modal = MODAL_NONE;
            if (chosen && app->active_repo_idx < app->ws.count) {
                char summary[256] = "";
                char *bname = strdup(chosen->name ? chosen->name : "");
                bool is_rem = chosen->is_remote;
                if (bname && *bname) {
                    knox_repo_checkout_branch(&app->ws.repos[app->active_repo_idx],
                                              bname, is_rem, summary, sizeof(summary));
                    app_set_status(app, "%s", summary);
                    app_ensure_repo_graph(app, app->active_repo_idx, true, true, true);
                    knox_explorer_refresh_git_status(&app->explorer, &app->ws);
                }
                free(bname);
            }
            return;
        }
        if (ev->type == TAM_KEY_BACKSPACE) {
            if (app->prompt_len > 0)
                app->prompt_buf[--app->prompt_len] = '\0';
            return;
        }
        if (ev->type == TAM_KEY_CHAR && !(ev->mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
            ev->codepoint >= 0x20 && ev->codepoint < 0x7f) {
            if (app->prompt_len + 1 < sizeof(app->prompt_buf)) {
                app->prompt_buf[app->prompt_len++] = (char)ev->codepoint;
                app->prompt_buf[app->prompt_len] = '\0';
                app->branch_cursor = 0;
                app->branch_top = 0;
            }
        }
        return;
    }

    if (app->modal == MODAL_STASH) {
        if (ev->type == TAM_KEY_UP ||
            (ev->type == TAM_KEY_CHAR && ev->codepoint == 'k')) {
            if (app->stash_cursor > 0)
                app->stash_cursor--;
            return;
        }
        if (ev->type == TAM_KEY_DOWN ||
            (ev->type == TAM_KEY_CHAR && ev->codepoint == 'j')) {
            if (app->stash_cursor + 1 < app->stash_count)
                app->stash_cursor++;
            return;
        }
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'n' || ev->codepoint == 'N' ||
             ev->codepoint == 's' || ev->codepoint == 'S')) {
            app->modal = MODAL_STASH_PUSH;
            app->prompt_buf[0] = '\0';
            app->prompt_len = 0;
            return;
        }
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'a' || ev->codepoint == 'A' ||
             ev->codepoint == 'p' || ev->codepoint == 'P')) {
            bool do_pop = (ev->codepoint == 'p' || ev->codepoint == 'P');
            if (app->stash_cursor < app->stash_count &&
                app->active_repo_idx < app->ws.count) {
                char summary[256] = "";
                char *ref = strdup(app->stash_list[app->stash_cursor].ref);
                app->modal = MODAL_NONE;
                if (ref) {
                    knox_repo_stash_apply(&app->ws.repos[app->active_repo_idx],
                                          ref, do_pop, summary, sizeof(summary));
                    app_set_status(app, "%s", summary);
                    app_ensure_repo_graph(app, app->active_repo_idx, true, true, true);
                    knox_explorer_refresh_git_status(&app->explorer, &app->ws);
                }
                free(ref);
            }
            return;
        }
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'd' || ev->codepoint == 'D' ||
             ev->codepoint == 'x' || ev->codepoint == 'X')) {
            if (app->stash_cursor < app->stash_count &&
                app->active_repo_idx < app->ws.count) {
                const knox_stash_entry_t *st = &app->stash_list[app->stash_cursor];
                snprintf(app->pending_stash_ref, sizeof(app->pending_stash_ref),
                         "%s", st->ref ? st->ref : "stash@{0}");
                app_set_status(app, "Confirm drop of %s (%s)? (y/N)",
                               app->pending_stash_ref,
                               st->message ? st->message : "");
                app->modal = MODAL_CONFIRM_STASH_DROP;
            }
            return;
        }
        if (ev->type == TAM_KEY_ENTER) {
            if (app->stash_cursor < app->stash_count &&
                app->active_repo_idx < app->ws.count) {
                const knox_repo_t *repo = &app->ws.repos[app->active_repo_idx];
                const knox_stash_entry_t *st = &app->stash_list[app->stash_cursor];
                char *ref = strdup(st->ref ? st->ref : "stash@{0}");
                app->modal = MODAL_NONE;
                if (ref) {
                    char *diff = knox_repo_diff_stash(repo, ref);
                    char title[64], bc[256];
                    snprintf(title, sizeof(title), "Stash %s", ref);
                    snprintf(bc, sizeof(bc), "%s > %s (%s)",
                             repo->name, ref, st->message ? st->message : "");
                    app_open_diff_in_pane_or_tab(app, title, repo->name, bc, diff);
                }
                free(ref);
            }
            return;
        }
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'q' || ev->codepoint == 'Q')) {
            app->modal = MODAL_NONE;
        }
        return;
    }

    if (app->modal == MODAL_GUIDANCE) {
        if (ev->type == TAM_KEY_TAB || ev->type == TAM_KEY_LEFT ||
            ev->type == TAM_KEY_RIGHT) {
            app->guidance_sort_by_command = !app->guidance_sort_by_command;
            app->guidance_top = 0;
            return;
        }
        if (ev->type == TAM_KEY_UP ||
            (ev->type == TAM_KEY_CHAR && !(ev->mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
             ev->codepoint == 'k')) {
            if (app->guidance_top > 0)
                app->guidance_top--;
            return;
        }
        if (ev->type == TAM_KEY_DOWN ||
            (ev->type == TAM_KEY_CHAR && !(ev->mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
             ev->codepoint == 'j')) {
            app->guidance_top++;
            return;
        }
        if (ev->type == TAM_KEY_PAGE_UP ||
            (ev->type == TAM_KEY_CHAR && !(ev->mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
             ev->codepoint == 'b')) {
            app->guidance_top = (app->guidance_top > 12) ? app->guidance_top - 12 : 0;
            return;
        }
        if (ev->type == TAM_KEY_PAGE_DOWN ||
            (ev->type == TAM_KEY_CHAR && !(ev->mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
             ev->codepoint == ' ')) {
            app->guidance_top += 12;
            return;
        }
        if (ev->type == TAM_KEY_HOME ||
            (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_ALT) &&
             ev->codepoint == '<')) {
            app->guidance_top = 0;
            return;
        }
        if (ev->type == TAM_KEY_END ||
            (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_ALT) &&
             ev->codepoint == '>')) {
            app->guidance_top = (size_t)-1;
            return;
        }
        if (ev->type == TAM_KEY_CHAR &&
            (ev->codepoint == 'q' || ev->codepoint == 'Q' ||
             ev->codepoint == 'g' || ev->codepoint == 'G'))
            app->modal = MODAL_NONE;
        return;
    }

    if (app->modal == MODAL_PALETTE) {
        if (ev->type == TAM_KEY_TAB) {
            /* Standalone TERM=linux modifier cycling in chord composer */
            if (app->chord_mods == 0)
                app->chord_mods = TAM_MOD_CTRL;
            else if (app->chord_mods == TAM_MOD_CTRL)
                app->chord_mods = TAM_MOD_ALT;
            else if (app->chord_mods == TAM_MOD_ALT)
                app->chord_mods = TAM_MOD_SHIFT;
            else if (app->chord_mods == TAM_MOD_SHIFT)
                app->chord_mods = TAM_MOD_CTRL | TAM_MOD_SHIFT;
            else if (app->chord_mods == (TAM_MOD_CTRL | TAM_MOD_SHIFT))
                app->chord_mods = TAM_MOD_ALT | TAM_MOD_SHIFT;
            else
                app->chord_mods = 0;
            return;
        }
        if (ev->type == TAM_KEY_UP) {
            if (app->palette_cursor > 0)
                app->palette_cursor--;
            return;
        }
        if (ev->type == TAM_KEY_DOWN) {
            app->palette_cursor++;
            return;
        }
        if (ev->type == TAM_KEY_ENTER) {
            size_t m = 0;
            for (size_t i = 0; i < WIDGET_COUNT; i++) {
                if (app->prompt_len == 0 ||
                    strcasestr(WIDGET_TABLE[i].name, app->prompt_buf) ||
                    strcasestr(WIDGET_TABLE[i].desc, app->prompt_buf) ||
                    strcasestr(WIDGET_TABLE[i].keys, app->prompt_buf)) {
                    if (m == app->palette_cursor) {
                        app->modal = MODAL_NONE;
                        app_execute_widget(app, WIDGET_TABLE[i].name);
                        return;
                    }
                    m++;
                }
            }
            app->modal = MODAL_NONE;
            return;
        }
    }

    /* Text editing inside MODAL_SEARCH, MODAL_COMMIT, or MODAL_PALETTE */
    if (ev->type == TAM_KEY_BACKSPACE) {
        if (app->prompt_len == 0 && app->modal == MODAL_PALETTE) {
            app->modal = MODAL_NONE;
            return;
        }
        if (app->prompt_len > 0)
            app->prompt_buf[--app->prompt_len] = '\0';
        if (app->modal == MODAL_SEARCH && app->focus == FOCUS_SIDEBAR_EXPLORER) {
            knox_explorer_set_filter(&app->explorer, app->prompt_buf, &app->ws);
            app->explorer_cursor = 0;
            app->explorer_top = 0;
        }
        return;
    }
    if (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_CTRL) &&
        (ev->codepoint == 'k' || ev->codepoint == 'K')) {
        app->prompt_len = 0;
        app->prompt_buf[0] = '\0';
        if (app->modal == MODAL_SEARCH && app->focus == FOCUS_SIDEBAR_EXPLORER) {
            knox_explorer_set_filter(&app->explorer, "", &app->ws);
            app->explorer_cursor = 0;
            app->explorer_top = 0;
        }
        return;
    }
    if (ev->type == TAM_KEY_ENTER) {
        if (app->modal == MODAL_COMMIT) {
            app->modal = MODAL_NONE;
            if (app->active_repo_idx < app->ws.count) {
                char summary[256] = "";
                knox_repo_commit_staged(&app->ws.repos[app->active_repo_idx],
                                        app->prompt_buf, summary, sizeof(summary));
                app_set_status(app, "%s", summary);
                app_ensure_repo_graph(app, app->active_repo_idx, true, true, true);
                knox_explorer_refresh_git_status(&app->explorer, &app->ws);
            }
            return;
        } else if (app->modal == MODAL_BRANCH_CREATE) {
            app->modal = MODAL_NONE;
            if (app->active_repo_idx < app->ws.count && app->prompt_len > 0) {
                char summary[256] = "";
                knox_repo_create_branch(&app->ws.repos[app->active_repo_idx],
                                        app->prompt_buf, NULL,
                                        summary, sizeof(summary));
                app_set_status(app, "%s", summary);
                app_ensure_repo_graph(app, app->active_repo_idx, true, true, true);
                knox_explorer_refresh_git_status(&app->explorer, &app->ws);
            }
            return;
        } else if (app->modal == MODAL_STASH_PUSH) {
            app->modal = MODAL_NONE;
            if (app->active_repo_idx < app->ws.count) {
                char summary[256] = "";
                knox_repo_stash_push(&app->ws.repos[app->active_repo_idx],
                                     app->prompt_len > 0 ? app->prompt_buf : NULL,
                                     true, summary, sizeof(summary));
                app_set_status(app, "%s", summary);
                app_ensure_repo_graph(app, app->active_repo_idx, true, true, true);
                knox_explorer_refresh_git_status(&app->explorer, &app->ws);
            }
            return;
        } else if (app->modal == MODAL_SEARCH) {
            app->modal = MODAL_NONE;
            if (app->focus == FOCUS_SIDEBAR_EXPLORER) {
                knox_explorer_set_filter(&app->explorer, app->prompt_buf, &app->ws);
                app->explorer_cursor = 0;
                app->explorer_top = 0;
                if (app->prompt_len == 0) {
                    app_set_status(app, "Cleared Explorer filter");
                } else {
                    size_t matches = app->explorer.flat_count > 0
                                     ? app->explorer.flat_count - 1 : 0;
                    app_set_status(app, "Explorer filter '%s': %zu item(s) (Esc to clear)",
                                   app->prompt_buf, matches);
                }
                return;
            }
            if (app->prompt_len == 0)
                return;
            knox_pane_t *ap = app_active_pane(app);
            if (app->focus == FOCUS_MAIN_PANE && ap &&
                ap->content == KNOX_PANE_VIEWER) {
                if (!knox_viewer_search(&ap->doc, app->prompt_buf, true, false))
                    app_set_status(app, "No match for '%s'", app->prompt_buf);
            } else if (app->focus == FOCUS_MAIN_PANE && ap &&
                       ap->content == KNOX_PANE_GRAPH) {
                for (size_t i = 0; i < app->graph.commit_count; i++) {
                    size_t idx = (ap->graph_cursor + 1 + i) % app->graph.commit_count;
                    const knox_commit_t *c = &app->graph.commits[idx];
                    if ((c->subject && strcasestr(c->subject, app->prompt_buf)) ||
                        (c->author && strcasestr(c->author, app->prompt_buf)) ||
                        strncasecmp(c->hash, app->prompt_buf, app->prompt_len) == 0) {
                        ap->graph_cursor = idx;
                        return;
                    }
                }
                app_set_status(app, "No commit matching '%s'", app->prompt_buf);
            } else if (app->focus == FOCUS_SIDEBAR_REPOS) {
                for (size_t i = 0; i < app->ws.count; i++) {
                    if (strcasestr(app->ws.repos[i].name, app->prompt_buf)) {
                        app->active_repo_idx = i;
                        return;
                    }
                }
            }
            return;
        }
    }
    if (ev->type == TAM_KEY_CHAR && !(ev->mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
        ev->codepoint >= 0x20 && ev->codepoint < 0x7f) {
        if (app->prompt_len + 1 < sizeof(app->prompt_buf)) {
            app->prompt_buf[app->prompt_len++] = (char)ev->codepoint;
            app->prompt_buf[app->prompt_len] = '\0';
            if (app->modal == MODAL_SEARCH && app->focus == FOCUS_SIDEBAR_EXPLORER) {
                knox_explorer_set_filter(&app->explorer, app->prompt_buf, &app->ws);
                app->explorer_cursor = 0;
                app->explorer_top = 0;
            }
        }
    }
}

/* True for the modals that take typed text. */
static bool modal_takes_text(int modal)
{
    return modal == MODAL_PALETTE || modal == MODAL_SEARCH || modal == MODAL_COMMIT ||
           modal == MODAL_BRANCH || modal == MODAL_BRANCH_CREATE ||
           modal == MODAL_STASH_PUSH;
}

static void handle_key_event(knox_app_t *app, const tam_key_event_t *ev)
{
    /* Pasted text goes only into a text prompt, as if typed: newlines and
     * tabs become spaces (every prompt is one line) and other control
     * bytes are dropped. It never reaches a command, so pasting cannot
     * run one. */
    if (ev->type == TAM_KEY_PASTE_BEGIN || ev->type == TAM_KEY_PASTE_END ||
        ev->type == TAM_KEY_MOUSE)
        return;
    if (ev->type == TAM_KEY_PASTE) {
        for (size_t i = 0; i < ev->len && modal_takes_text(app->modal); i++) {
            unsigned char c = (unsigned char)ev->text[i];
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
            if (c < 0x20 || c >= 0x7f)
                continue;
            tam_key_event_t ch = {.type = TAM_KEY_CHAR, .codepoint = c};
            handle_modal_key(app, &ch);
        }
        return;
    }

    if (app->modal != MODAL_NONE) {
        handle_modal_key(app, ev);
        return;
    }

    /* 0. User keymap.ini overrides (take precedence over built-in defaults) */
    const char *custom_widget = knox_keymap_lookup(&app->keymap, ev);
    if (custom_widget) {
        if (strcmp(custom_widget, "unbind") == 0 ||
            strcmp(custom_widget, "none") == 0)
            return;
        app_execute_widget(app, custom_widget);
        return;
    }

    /* 1. Universal Ctrl map (from tam-idol/keys/keybindings.md) */
    if (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_CTRL)) {
        uint32_t c = ev->codepoint < 0x80 ? (uint32_t)tolower((int)ev->codepoint)
                                          : ev->codepoint;
        switch (c) {
        case 'q': app_execute_widget(app, "quit"); return;
        case 'g': app_execute_widget(app, "guidance"); return;
        case 'e': app_execute_widget(app, "execute"); return;
        case 'f': app_execute_widget(app, "find"); return;
        case 'u': app_execute_widget(app, "update"); return;
        case 'd': app_execute_widget(app, "close"); return;
        case 'n': app_execute_widget(app, "new"); return;
        case 'c': app_execute_widget(app, "copy"); return;
        case 's': app_execute_widget(app, "commit-prompt"); return;
        case 'l': return; /* Redraws at end of loop */
        case 'p': /* Pause: stop until continued, then restore our modes. */
            (void)tam_tty_job_stop(&app->tty);
            return;
        default:  return; /* No double-acting Ctrl codes */
        }
    }

    /* 2. Alt chords */
    if (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_ALT)) {
        uint32_t c = ev->codepoint < 0x80 ? (uint32_t)tolower((int)ev->codepoint)
                                          : ev->codepoint;
        switch (c) {
        case '1': app_execute_widget(app, "view-source-control"); return;
        case '2': app_execute_widget(app, "view-explorer"); return;
        case '3': app_execute_widget(app, "view-graph"); return;
        case 'b': app_execute_widget(app, "toggle-sidebar"); return;
        case 'a': app_execute_widget(app, "fetch-all"); return;
        case 's': app_execute_widget(app, "repo-sync"); return;
        case 'c': app_execute_widget(app, "commit-prompt"); return;
        case 'k': app_execute_widget(app, "branch-modal"); return;
        case 'z': app_execute_widget(app, "stash-modal"); return;
        case 'd': app_execute_widget(app, "toggle-diff-split"); return;
        case 'e': app_execute_widget(app, "open-in-editor"); return;
        case 'v': app_execute_widget(app, "split-vertical"); return;
        case 'h': app_execute_widget(app, "split-horizontal"); return;
        case 'w': app_execute_widget(app, "focus-other-pane"); return;
        case 'r': app_execute_widget(app, "toggle-render-mode"); return;
        case 'f': app_execute_widget(app, "cycle-branch-filter"); return;
        case '[': app_execute_widget(app, "tab-prev"); return;
        case ']': app_execute_widget(app, "tab-next"); return;
        case '<': app_move_selection(app, -100000); return;
        case '>': app_move_selection(app, 100000); return;
        default:  return;
        }
    }

    /* 3. Navigation & Single-key read-only bindings */
    switch (ev->type) {
    case TAM_KEY_TAB:
        app_execute_widget(app, (ev->mods & TAM_MOD_SHIFT) ? "focus-prev" : "focus-next");
        return;
    case TAM_KEY_UP:
        app_move_selection(app, -1);
        return;
    case TAM_KEY_DOWN:
        app_move_selection(app, 1);
        return;
    case TAM_KEY_PAGE_UP:
        app_move_selection(app, -12);
        return;
    case TAM_KEY_PAGE_DOWN:
        app_move_selection(app, 12);
        return;
    case TAM_KEY_HOME:
        app_move_selection(app, -100000);
        return;
    case TAM_KEY_END:
        app_move_selection(app, 100000);
        return;
    case TAM_KEY_LEFT:
        if (app->focus == FOCUS_SIDEBAR_EXPLORER) {
            if (app->explorer_cursor < app->explorer.flat_count) {
                knox_tree_node_t *n = app->explorer.flat[app->explorer_cursor];
                if (n && n->is_dir && n->expanded)
                    knox_explorer_toggle_node(&app->explorer, app->explorer_cursor, &app->ws);
            }
        } else if (app->focus == FOCUS_MAIN_PANE) {
            if (app->sidebar_visible && app->sidebar_mode == SIDEBAR_EXPLORER) {
                app->focus = FOCUS_SIDEBAR_EXPLORER;
            } else {
                knox_pane_t *ap = app_active_pane(app);
                if (ap && ap->content == KNOX_PANE_VIEWER && !ap->doc.render_mode) {
                    if (ap->doc.left_col >= 4)
                        ap->doc.left_col -= 4;
                    else
                        ap->doc.left_col = 0;
                }
            }
        }
        return;
    case TAM_KEY_RIGHT:
        if (app->focus == FOCUS_SIDEBAR_REPOS) {
            if (app->active_repo_idx < app->ws.count)
                app_execute_widget(app, "explore-repo");
        } else if (app->focus == FOCUS_SIDEBAR_EXPLORER) {
            if (app->explorer_cursor < app->explorer.flat_count) {
                knox_tree_node_t *n = app->explorer.flat[app->explorer_cursor];
                if (n && n->is_dir) {
                    if (!n->expanded)
                        knox_explorer_toggle_node(&app->explorer, app->explorer_cursor, &app->ws);
                } else if (n) {
                    app_open_file_in_tab(app, n->abs_path);
                }
            }
        } else if (app->focus == FOCUS_MAIN_PANE) {
            if (!(app->sidebar_visible && app->sidebar_mode == SIDEBAR_EXPLORER)) {
                knox_pane_t *ap = app_active_pane(app);
                if (ap && ap->content == KNOX_PANE_VIEWER && !ap->doc.render_mode)
                    ap->doc.left_col += 4;
            }
        }
        return;
    case TAM_KEY_ENTER:
        app_execute_widget(app, "open-or-inspect");
        return;
    case TAM_KEY_BACKSPACE:
        if (app->focus == FOCUS_SIDEBAR_EXPLORER)
            app_execute_widget(app, "view-source-control");
        return;
    case TAM_KEY_ESC:
        app_execute_widget(app, "cancel-mode");
        return;
    case TAM_KEY_CHAR:
        switch (ev->codepoint) {
        case 'q':
        case 'Q':
            app_execute_widget(app, "quit");
            return;
        case 'g':
        case 'G':
            app_execute_widget(app, "guidance");
            return;
        case 'j':
            app_move_selection(app, 1);
            return;
        case 'k':
            app_move_selection(app, -1);
            return;
        case ' ':
            app_execute_widget(app, "stage-or-page-down");
            return;
        case 'b':
            app_move_selection(app, -12);
            return;
        case 'B':
            app_execute_widget(app, "cycle-branch-filter");
            return;
        case 's':
        case 'S':
            app_execute_widget(app, "repo-sync");
            return;
        case 'u':
            app_execute_widget(app, "update");
            return;
        case 'f':
            app_execute_widget(app, "repo-fetch");
            return;
        case 'F':
            app_execute_widget(app, "fetch-all");
            return;
        case 'r':
        case 'R':
            app_execute_widget(app, "toggle-render-mode");
            return;
        case 'd':
        case 'D':
            app_execute_widget(app, "toggle-diff-split");
            return;
        case 'c':
        case 'C':
            app_execute_widget(app, "branch-modal");
            return;
        case 'z':
        case 'Z':
            app_execute_widget(app, "stash-modal");
            return;
        case '/':
            app_execute_widget(app, "find");
            return;
        case 'n':
            app_execute_widget(app, "find-next");
            return;
        case 'N':
            app_execute_widget(app, "find-prev");
            return;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '0': {
            char wname[8];
            snprintf(wname, sizeof(wname), "tab-%c", (char)ev->codepoint);
            app_execute_widget(app, wname);
            return;
        }
        case 'v':
            /* In commit detail inspector, 'v' views the full file snapshot at that commit */
            if (app->focus == FOCUS_MAIN_PANE) {
                knox_pane_t *ap = app_active_pane(app);
                if (ap && ap->content == KNOX_PANE_COMMIT_DETAIL &&
                    ap->repo_idx < app->ws.count &&
                    ap->inspect_file_cursor < app->details.file_count) {
                    const knox_repo_t *repo = &app->ws.repos[ap->repo_idx];
                    const char *rel = app->details.files[ap->inspect_file_cursor].path;
                    char *snap = knox_repo_show_commit_file(repo, app->details.hash, rel);
                    if (snap) {
                        char title[64], bc[256];
                        snprintf(title, sizeof(title), "%s@%.7s", rel, app->details.hash);
                        snprintf(bc, sizeof(bc), "%s > %s (at %.8s)",
                                 repo->name, rel, app->details.hash);
                        knox_pane_t *vp = knox_pane_new(KNOX_PANE_VIEWER, ap->repo_idx);
                        if (vp) {
                            knox_lang_t lang = knox_syntax_detect(rel, snap, strlen(snap));
                            knox_viewer_open_buffer(&vp->doc, KNOX_DOC_FILE,
                                                    title, repo->name, bc,
                                                    snap, strlen(snap), lang);
                            app_add_tab(app, title, vp);
                        }
                        free(snap);
                    }
                }
            }
            return;
        default:
            return;
        }
    default:
        return;
    }
}

/* ========================================================================= */
/* Public Entry Points                                                       */
/* ========================================================================= */

int knox_run_workbench(const char *workspace_dir, knox_options_t opts)
{
    knox_app_t app = {0};
    app.opts = opts;
    app.sidebar_visible = true;
    app.sidebar_mode = SIDEBAR_SOURCE_CONTROL;
    app.focus = FOCUS_SIDEBAR_REPOS;
    app.running = true;

    if (tam_tty_open(&app.tty, STDIN_FILENO, STDOUT_FILENO) != 0 ||
        !app.tty.interactive) {
        fprintf(stderr, "tam-knox: standard input and output must be a terminal\n");
        return 1;
    }

    knox_keymap_init(&app.keymap);
    knox_keymap_load_default(&app.keymap);
    if (app.keymap.warning_count > 0) {
        app_set_status(&app, "%s (%zu warning(s))",
                       app.keymap.last_warning, app.keymap.warning_count);
    }

    knox_workspace_init(&app.ws, workspace_dir);
    knox_discover_repos(&app.ws, 2);
    knox_explorer_init(&app.explorer, app.ws.root_path, false);
    knox_explorer_refresh_git_status(&app.explorer, &app.ws);
    knox_graph_init(&app.graph);

    knox_pane_t *graph_pane = knox_pane_new(KNOX_PANE_GRAPH, 0);
    app_add_tab(&app, "Git Graph", graph_pane);
    if (app.ws.count > 0)
        app_ensure_repo_graph(&app, 0, true, true, true);

    struct sigaction sa = {0};
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);

    tam_tty_raw(&app.tty, true);
    tam_tty_alt_screen(&app.tty, true);
    tam_tty_cursor_visible(&app.tty, false);
    tam_tty_paste(&app.tty, true);
    /* SIGTERM, SIGHUP, a crash or an exit without cleanup put the terminal
     * back as it was. */
    (void)tam_tty_install_restore(&app.tty);
    tam_key_init(&app.keys);

    app_draw(&app);

    unsigned char inbuf[256];
    while (app.running) {
        if (g_winch_flag) {
            g_winch_flag = 0;
            tam_tty_size(&app.tty, NULL, NULL);
            app_draw(&app);
        }

        int n = tam_tty_read(&app.tty, inbuf, sizeof(inbuf), 50);
        if (n < 0)
            break;
        if (n == 0) {
            tam_key_event_t tev;
            if (tam_key_timeout(&app.keys, &tev) == 1) {
                handle_key_event(&app, &tev);
                app_draw(&app);
            }
            continue;
        }

        /* The key parser holds less than one read, so feed what it takes,
         * handle those keys, and feed the rest. */
        const unsigned char *rest = inbuf;
        size_t left = (size_t)n;
        bool handled = false;
        while (left > 0 && app.running) {
            int took = tam_key_feed(&app.keys, rest, left);
            if (took < 0)
                break;
            rest += took;
            left -= (size_t)took;
            tam_key_event_t ev;
            while (app.running && tam_key_next(&app.keys, &ev) == 1) {
                handle_key_event(&app, &ev);
                handled = true;
            }
        }
        if (handled)
            app_draw(&app);
    }

    tam_tty_close(&app.tty);

    for (size_t i = 0; i < app.tab_count; i++)
        knox_split_free_tree(app.tabs[i].root);
    knox_branch_list_free(app.branch_list, app.branch_count);
    knox_stash_list_free(app.stash_list, app.stash_count);
    knox_commit_details_free(&app.details);
    knox_graph_free(&app.graph);
    knox_explorer_free(&app.explorer);
    knox_workspace_free(&app.ws);
    knox_keymap_free(&app.keymap);
    return 0;
}

int knox_print_status_table(const char *workspace_dir, knox_options_t opts)
{
    knox_workspace_t ws;
    knox_workspace_init(&ws, workspace_dir);
    knox_discover_repos(&ws, 2);

    printf("%-28s %-14s %-10s %-10s %s\n",
           "REPOSITORY", "BRANCH", "STATE", "SYNC", "CHANGED FILES");
    printf("%-28s %-14s %-10s %-10s %s\n",
           "----------------------------", "--------------",
           "----------", "----------", "-------------");

    for (size_t i = 0; i < ws.count; i++) {
        const knox_repo_t *r = &ws.repos[i];
        char sync[32];
        if (r->ahead > 0 || r->behind > 0)
            snprintf(sync, sizeof(sync), "+%d/-%d", r->ahead, r->behind);
        else
            snprintf(sync, sizeof(sync), "synced");

        tam_str_t files = tam_str_new("");
        for (size_t c = 0; c < r->change_count && c < 5; c++) {
            if (c > 0)
                files = tam_str_cat(files, ", ");
            files = tam_str_cat(files, r->changes[c].path);
        }
        if (r->change_count > 5)
            files = tam_str_cat_printf(files, " (+%zu more)", r->change_count - 5);

        if (opts.color) {
            printf("%-28s \033[36m%-14s\033[0m %s%-10s\033[0m %-10s %s\n",
                   r->name,
                   r->branch ? r->branch : "?",
                   r->dirty ? "\033[33m" : "\033[32m",
                   r->dirty ? "dirty" : "clean",
                   sync,
                   r->dirty ? files : "-");
        } else {
            printf("%-28s %-14s %-10s %-10s %s\n",
                   r->name,
                   r->branch ? r->branch : "?",
                   r->dirty ? "dirty" : "clean",
                   sync,
                   r->dirty ? files : "-");
        }
        tam_str_free(files);
    }

    knox_workspace_free(&ws);
    return 0;
}

int knox_print_repo_graph(const char *repo_dir, size_t max_commits,
                          knox_options_t opts)
{
    knox_workspace_t ws;
    knox_workspace_init(&ws, repo_dir);
    knox_discover_repos(&ws, 1);
    if (ws.count == 0) {
        fprintf(stderr, "tam-knox: no Git repository found at %s\n", repo_dir);
        knox_workspace_free(&ws);
        return 1;
    }

    knox_graph_t g;
    knox_graph_init(&g);
    if (knox_repo_load_graph(&ws.repos[0], &g, max_commits ? max_commits : 30,
                             true, true, opts.ascii) != 0) {
        knox_graph_free(&g);
        knox_workspace_free(&ws);
        return 1;
    }

    for (size_t i = 0; i < g.commit_count; i++) {
        const knox_commit_t *c = &g.commits[i];
        char gcells[512];
        int gvis = 0;
        knox_graph_format_row(&g, i, 24, opts.color, opts.linux_vt,
                              gcells, sizeof(gcells), &gvis);
        char date_str[32];
        format_commit_date(c->timestamp, date_str, sizeof(date_str));

        printf("%s ", gcells);
        for (size_t h = 0; h < c->head_count; h++)
            printf("[%s] ", c->heads[h]);
        for (size_t rm = 0; rm < c->remote_count; rm++)
            printf("[%s] ", c->remotes[rm]);
        for (size_t t = 0; t < c->tag_count; t++)
            printf("[tag: %s] ", c->tags[t]);
        printf("%s  (%s, %s, %.8s)\n",
               c->subject ? c->subject : "",
               date_str,
               c->author ? c->author : "",
               c->hash);
    }

    knox_graph_free(&g);
    knox_workspace_free(&ws);
    return 0;
}
