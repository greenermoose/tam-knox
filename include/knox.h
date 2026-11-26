#ifndef KNOX_H
#define KNOX_H

#include <tam_render.h>
#include <tam_render.h>

#include <tam.h>
#include <tam_key.h>
#include <tam_pager.h>
#include <tam_tty.h>
#include <tam_uni.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifndef KNOX_VERSION
#define KNOX_VERSION "1.1.1"
#endif

/* ========================================================================= */
/* 1. Multi-Repository Discovery, Status, Stage, Commit, Sync (src/git.c)    */
/* ========================================================================= */

typedef enum {
    KNOX_CHANGE_NONE      = ' ',
    KNOX_CHANGE_MODIFIED  = 'M',
    KNOX_CHANGE_ADDED     = 'A',
    KNOX_CHANGE_DELETED   = 'D',
    KNOX_CHANGE_RENAMED   = 'R',
    KNOX_CHANGE_COPIED    = 'C',
    KNOX_CHANGE_UNTRACKED = 'U',
    KNOX_CHANGE_CONFLICT  = '!'
} knox_change_type_t;

typedef struct {
    char              *path;            /* Path relative to repo root */
    char              *old_path;        /* Non-NULL for renames */
    knox_change_type_t index_status;    /* Staged status ('M','A','D','R',' ') */
    knox_change_type_t worktree_status; /* Unstaged status ('M','D','U',' ') */
    bool               staged;          /* True if index_status != ' ' && != 'U' */
    bool               unstaged;        /* True if worktree_status != ' ' */
    int                additions;       /* >= 0, or -1 if binary/unknown */
    int                deletions;
} knox_file_change_t;

typedef struct {
    char               *name;           /* Display name, e.g. "tam-clam" */
    char               *root_path;      /* Canonical absolute path */
    char               *rel_path;       /* Relative to workspace root */
    char               *branch;         /* Checked-out branch or "(detached)" */
    char               *upstream;       /* Upstream branch e.g. "origin/dev" */
    char                head_hash[41];  /* Full 40-hex OID or empty */
    bool                dirty;          /* True if change_count > 0 */
    size_t              staged_count;
    size_t              unstaged_count;
    int                 ahead;          /* Commits ahead of upstream */
    int                 behind;         /* Commits behind upstream */
    knox_file_change_t *changes;
    size_t              change_count;
    size_t              change_cap;
    char               *last_sync_msg;  /* Summary of last sync/commit */
} knox_repo_t;

typedef struct {
    char        *root_path;
    knox_repo_t *repos;
    size_t       count;
    size_t       cap;
} knox_workspace_t;

void knox_repo_free_changes(knox_repo_t *repo);
void knox_repo_free(knox_repo_t *repo);
void knox_workspace_init(knox_workspace_t *ws, const char *root_path);
void knox_workspace_free(knox_workspace_t *ws);

/* Discover all git repositories under ws->root_path (up to max_depth) and
 * refresh their branch, dirty state, changed files, and ahead/behind counts. */
int  knox_discover_repos(knox_workspace_t *ws, int max_depth);
int  knox_repo_refresh_status(knox_repo_t *repo);

/* Find the repository owning `abs_path` (longest prefix match), or NULL. */
knox_repo_t *knox_workspace_find_repo_for_path(knox_workspace_t *ws,
                                               const char *abs_path);

/* Check if a branch requires confirmation before pushing ("main" or "test"). */
bool knox_branch_needs_push_confirm(const char *branch);

/* Stage or unstage a single repository-relative path. */
int  knox_repo_stage_file(knox_repo_t *repo, const char *rel_path,
                          char *errbuf, size_t errcap);
int  knox_repo_unstage_file(knox_repo_t *repo, const char *rel_path,
                            char *errbuf, size_t errcap);

/* Commit ONLY explicitly staged paths in `repo` (never git commit -a). */
int  knox_repo_commit_staged(knox_repo_t *repo, const char *message,
                             char *out_summary, size_t sumcap);

/* Sync with remote: git fetch --prune, pull --ff-only if behind, push if ahead.
 * If allow_protected_push is false and branch is "main" or "test" with
 * ahead > 0, returns 1 (caller should prompt user) without pushing. */
int  knox_repo_sync(knox_repo_t *repo, bool allow_protected_push,
                    char *out_summary, size_t sumcap);

/* Multi-repo batch fetch (src/git.c): non-destructive git fetch --prune
 * across all discovered repositories with configured remotes. */
typedef struct {
    size_t total;      /* Total repos in workspace */
    size_t fetched;    /* Repos successfully fetched */
    size_t skipped;    /* Repos with no remote configured */
    size_t failed;     /* Repos where git fetch failed */
    size_t behind_any; /* Repos with behind > 0 after fetch */
} knox_batch_fetch_result_t;

typedef void (*knox_fetch_progress_fn)(size_t idx, size_t total,
                                       const knox_repo_t *repo,
                                       void *user);

/* Fetch a single repo (git fetch --prune). Returns 0 on success,
 * 1 if no remote is configured (skipped), -1 on failure. */
int  knox_repo_fetch(knox_repo_t *repo, char *out_summary, size_t sumcap);

/* Fetch all repos in `ws` with configured remotes and refresh status. */
int  knox_workspace_fetch_all(knox_workspace_t *ws,
                              knox_fetch_progress_fn progress_cb,
                              void *user,
                              knox_batch_fetch_result_t *out_res,
                              char *out_summary, size_t sumcap);

/* Generate unified diff text (caller frees returned char*). */
char *knox_repo_diff_working_file(const knox_repo_t *repo,
                                  const knox_file_change_t *chg);
char *knox_repo_diff_commit_file(const knox_repo_t *repo,
                                 const char *commit_hash,
                                 const char *rel_path);
char *knox_repo_show_commit_file(const knox_repo_t *repo,
                                 const char *commit_hash,
                                 const char *rel_path);

/* Branch checkout & creation (src/git.c) */
typedef struct {
    char *name;          /* e.g. "dev" or "origin/feature" */
    char *upstream;      /* e.g. "origin/dev" or NULL */
    char  hash[41];      /* Short or full OID */
    char *subject;       /* Tip commit subject */
    bool  is_remote;     /* False = refs/heads, True = refs/remotes */
    bool  is_current;    /* True if checked out HEAD */
} knox_branch_info_t;

int  knox_repo_list_branches(const knox_repo_t *repo,
                             knox_branch_info_t **out_list,
                             size_t *out_count);
void knox_branch_list_free(knox_branch_info_t *list, size_t count);
int  knox_repo_checkout_branch(knox_repo_t *repo, const char *branch_name,
                               bool is_remote, char *out_summary, size_t sumcap);
int  knox_repo_create_branch(knox_repo_t *repo, const char *new_branch,
                             const char *start_point,
                             char *out_summary, size_t sumcap);

/* Stash list management (src/git.c) */
typedef struct {
    char   *ref;         /* e.g. "stash@{0}" */
    char    hash[41];
    char   *branch;      /* Parsed branch from stash subject if present */
    char   *message;     /* Full stash summary */
    int64_t timestamp;
} knox_stash_entry_t;

int  knox_repo_list_stashes(const knox_repo_t *repo,
                            knox_stash_entry_t **out_list,
                            size_t *out_count);
void knox_stash_list_free(knox_stash_entry_t *list, size_t count);
int  knox_repo_stash_push(knox_repo_t *repo, const char *message,
                          bool include_untracked,
                          char *out_summary, size_t sumcap);
int  knox_repo_stash_apply(knox_repo_t *repo, const char *stash_ref,
                           bool pop, char *out_summary, size_t sumcap);
int  knox_repo_stash_drop(knox_repo_t *repo, const char *stash_ref,
                          char *out_summary, size_t sumcap);
char *knox_repo_diff_stash(const knox_repo_t *repo, const char *stash_ref);

/* ========================================================================= */
/* 2. Git Graph Data & mhutchie.git-graph Layout Engine (src/graph.c)        */
/* ========================================================================= */

#define KNOX_MAX_PARENTS 16
#define KNOX_UNCOMMITTED_HASH "*"

typedef struct {
    char  hash[41];
    char  parents[KNOX_MAX_PARENTS][41];
    size_t parent_count;
    char *author;
    char *email;
    int64_t timestamp;
    char *subject;
    char **heads;         /* Local branch names pointing here */
    size_t head_count;
    char **remotes;       /* Remote branch names pointing here */
    size_t remote_count;
    char **tags;          /* Tag names pointing here */
    size_t tag_count;
    bool  is_head;
    bool  is_uncommitted;
    bool  is_stash;
} knox_commit_t;

typedef struct {
    char               hash[41];
    char              *author;
    char              *author_email;
    int64_t            author_date;
    char              *committer;
    char              *committer_email;
    int64_t            committer_date;
    char              *body;
    knox_file_change_t *files;
    size_t             file_count;
} knox_commit_details_t;

typedef struct {
    int x;
    int y;
} knox_point_t;

typedef struct {
    knox_point_t p1;
    knox_point_t p2;
    bool         is_committed;
    bool         locked_first;
} knox_line_seg_t;

typedef struct {
    int              colour;
    int              end_y;
    int              num_uncommitted;
    knox_line_seg_t *lines;
    size_t           line_count;
    size_t           line_cap;
} knox_branch_t;

typedef struct {
    int  connects_to; /* target vertex id, or -2 if unoccupied */
    int  branch_id;   /* index into branches[], or -1 */
} knox_conn_t;

typedef struct {
    int          id;
    int          x;
    int          next_x;
    int         *parents;      /* vertex indices, or -1 for NULL_VERTEX */
    size_t       parent_count;
    size_t       parent_cap;
    size_t       next_parent;
    int         *children;
    size_t       child_count;
    size_t       child_cap;
    int          branch_id;    /* -1 if not on a branch */
    bool         is_committed;
    bool         is_current;
    bool         is_stash;
    knox_conn_t *connections;
    size_t       conn_cap;
} knox_vertex_t;

typedef struct {
    char    ch[8];     /* UTF-8 glyph (e.g. "│", "●", "╭", or ASCII "|", "*") */
    uint8_t colour;    /* 0..7 palette index */
    bool    is_node;
    bool    is_uncommitted;
} knox_graph_cell_t;

typedef struct {
    knox_graph_cell_t *cells; /* width = max_cols */
    int                width;
} knox_graph_row_t;

typedef struct {
    knox_commit_t *commits;
    size_t         commit_count;
    size_t         commit_cap;
    char           head_hash[41];
    knox_vertex_t *vertices;
    size_t         vertex_count;
    knox_branch_t *branches;
    size_t         branch_count;
    size_t         branch_cap;
    int           *available_colours;
    size_t         avail_colour_count;
    size_t         avail_colour_cap;
    int            max_lane_x;
    knox_graph_row_t *rows;
    size_t         row_count;
} knox_graph_t;

void knox_graph_init(knox_graph_t *g);
void knox_graph_free(knox_graph_t *g);

/* Load commits and refs from `repo` into `g`, then run layout + rasterizer. */
int  knox_repo_load_graph(const knox_repo_t *repo, knox_graph_t *g,
                          size_t max_commits, bool all_branches,
                          bool include_remotes, bool ascii);

/* Run mhutchie.git-graph Vertex/Branch/determinePath layout on g->commits. */
int  knox_graph_build_layout(knox_graph_t *g);

/* Rasterize g->vertices and g->branches into g->rows character cells. */
int  knox_graph_rasterize(knox_graph_t *g, bool ascii);

/* Load detailed metadata and changed files (+add/-del) for a commit. */
int  knox_repo_load_commit_details(const knox_repo_t *repo,
                                   const char *commit_hash,
                                   knox_commit_details_t *out);
void knox_commit_details_free(knox_commit_details_t *d);

/* Format a single graph row's cells into `out` with optional ANSI colors. */
size_t knox_graph_format_row(const knox_graph_t *g, size_t row_idx,
                             int max_cell_cols, bool color, bool linux_vt,
                             char *out, size_t out_cap, int *out_vis_cols);

/* ========================================================================= */
/* 3. Workspace & Repository Folder Explorer (src/explorer.c)                */
/* ========================================================================= */

typedef struct knox_tree_node {
    char                  *name;
    char                  *abs_path;
    bool                   is_dir;
    bool                   is_repo_root;
    bool                   expanded;
    bool                   loaded;
    int                    depth;
    knox_change_type_t     git_status;     /* 'M','A','D','R','U' or ' ' */
    bool                   has_dirty_desc; /* Directory contains dirty files */
    struct knox_tree_node *parent;
    struct knox_tree_node **children;
    size_t                 child_count;
    size_t                 child_cap;
} knox_tree_node_t;

typedef struct {
    knox_tree_node_t  *root;
    knox_tree_node_t **flat;       /* Flattened visible rows */
    size_t             flat_count;
    size_t             flat_cap;
    bool               show_hidden;
    char               filter[128]; /* Case-insensitive substring filter */
} knox_explorer_t;

int  knox_explorer_init(knox_explorer_t *ex, const char *root_path,
                        bool show_hidden);
void knox_explorer_free(knox_explorer_t *ex);
int  knox_explorer_toggle_node(knox_explorer_t *ex, size_t flat_idx,
                               const knox_workspace_t *ws);
void knox_explorer_refresh_git_status(knox_explorer_t *ex,
                                      const knox_workspace_t *ws);
int  knox_explorer_set_filter(knox_explorer_t *ex, const char *query,
                              const knox_workspace_t *ws);
void knox_explorer_clear_filter(knox_explorer_t *ex);
int  knox_explorer_rebuild(knox_explorer_t *ex, const knox_workspace_t *ws);
int  knox_explorer_reveal_path(knox_explorer_t *ex, const char *target_abs_path,
                               const knox_workspace_t *ws,
                               size_t *out_flat_idx);

/* ========================================================================= */
/* 4. Zero-Dependency C17 Syntax Lexer (src/syntax.c)                        */
/* ========================================================================= */

typedef enum {
    KNOX_LANG_PLAIN = 0,
    KNOX_LANG_C,
    KNOX_LANG_SHELL,
    KNOX_LANG_PYTHON,
    KNOX_LANG_JS,
    KNOX_LANG_JSON,
    KNOX_LANG_YAML,
    KNOX_LANG_TOML_INI,
    KNOX_LANG_MAKEFILE,
    KNOX_LANG_MARKDOWN,
    KNOX_LANG_HTML,
    KNOX_LANG_RUST,
    KNOX_LANG_GO,
    KNOX_LANG_DIFF
} knox_lang_t;

typedef enum {
    KNOX_TOK_NORMAL = 0,
    KNOX_TOK_KEYWORD,
    KNOX_TOK_TYPE,
    KNOX_TOK_STRING,
    KNOX_TOK_NUMBER,
    KNOX_TOK_COMMENT,
    KNOX_TOK_PREPROC,
    KNOX_TOK_HEADING,
    KNOX_TOK_KEY,
    KNOX_TOK_DIFF_ADD,
    KNOX_TOK_DIFF_DEL,
    KNOX_TOK_DIFF_HUNK,
    KNOX_TOK_DIFF_HDR
} knox_token_kind_t;

typedef enum {
    KNOX_LEX_NORMAL = 0,
    KNOX_LEX_BLOCK_COMMENT,
    KNOX_LEX_TRIPLE_DQ,
    KNOX_LEX_TRIPLE_SQ
} knox_lex_state_t;

knox_lang_t knox_syntax_detect(const char *path, const char *first_line,
                               size_t first_len);
const char *knox_syntax_lang_name(knox_lang_t lang);

/* Highlight a single line of UTF-8 text (`line[0..len)`), populating
 * `out_tokens[0..len)` with a `knox_token_kind_t` per byte and returning
 * the end-of-line multi-line lexer state. */
knox_lex_state_t knox_syntax_highlight_line(knox_lang_t lang,
                                            knox_lex_state_t in_state,
                                            const char *line, size_t len,
                                            uint8_t *out_tokens);

/* SGR color escape sequence for a token kind. */
const char *knox_syntax_token_sgr(knox_token_kind_t kind, bool linux_vt);

/* ========================================================================= */
/* 5. Dual-Mode File & Diff Viewer (src/viewer.c)                            */
/* ========================================================================= */

typedef enum {
    KNOX_DOC_FILE = 0,
    KNOX_DOC_DIFF
} knox_doc_kind_t;

typedef enum {
    KNOX_SBS_CONTEXT = 0,
    KNOX_SBS_HEADER,
    KNOX_SBS_HUNK,
    KNOX_SBS_CHANGE
} knox_sbs_row_kind_t;

typedef struct {
    knox_sbs_row_kind_t kind;
    int                 old_lineno; /* > 0, or 0 if empty/filler */
    int                 new_lineno; /* > 0, or 0 if empty/filler */
    size_t              old_off;    /* Byte offset in raw_text */
    size_t              old_len;    /* Byte length (excluding leading -/+) */
    bool                old_changed;
    size_t              new_off;
    size_t              new_len;
    bool                new_changed;
} knox_sbs_row_t;

typedef struct {
    knox_doc_kind_t    kind;
    char              *title;         /* Tab title, e.g. "ROADMAP.md" */
    char              *repo_name;     /* Disambiguator, e.g. "tam" */
    char              *abs_path;      /* File path on disk, or NULL for diff */
    char              *breadcrumb;    /* "tam > ROADMAP.md" */
    char              *raw_text;      /* Full UTF-8 buffer */
    size_t             raw_len;
    size_t            *line_offsets;  /* Byte offset of each line in raw_text */
    size_t            *line_lengths;  /* Byte length of each line (no '\n') */
    uint8_t           *line_states;   /* knox_lex_state_t entering each line */
    size_t             line_count;
    knox_lang_t        lang;
    enum tam_render_format   markup_fmt;    /* TAM_RENDER_FORMAT_MARKDOWN / HTML / MERMAID */
    bool               render_mode;   /* True = rendered MD/HTML/Mermaid */
    int                rendered_width;
    bool               rendered_ascii;
    struct tam_render_buf        rendered_buf;
    size_t            *rend_offsets;
    size_t            *rend_lengths;
    size_t             rend_line_count;
    ssize_t            focused_link;
    /* Side-by-Side Split Diff mode (for KNOX_DOC_DIFF) */
    bool               side_by_side;
    knox_sbs_row_t    *sbs_rows;
    size_t             sbs_count;
    /* Viewport state */
    size_t             top_line;
    size_t             cursor_line;
    int                left_col;
    bool               wrap_lines;
    char               search_query[128];
} knox_viewer_doc_t;

int  knox_viewer_open_file(knox_viewer_doc_t *doc, const char *abs_path,
                           const char *repo_name, bool prefer_render,
                           int width, bool ascii);
int  knox_viewer_open_buffer(knox_viewer_doc_t *doc, knox_doc_kind_t kind,
                             const char *title, const char *repo_name,
                             const char *breadcrumb, const char *text,
                             size_t len, knox_lang_t lang);
void knox_viewer_free(knox_viewer_doc_t *doc);
int  knox_viewer_toggle_render(knox_viewer_doc_t *doc, int width, bool ascii);
int  knox_viewer_ensure_rendered(knox_viewer_doc_t *doc, int width, bool ascii);
int  knox_viewer_build_sbs(knox_viewer_doc_t *doc);
int  knox_viewer_toggle_side_by_side(knox_viewer_doc_t *doc);
bool knox_viewer_search(knox_viewer_doc_t *doc, const char *query,
                        bool forward, bool step);

/* ========================================================================= */
/* 6. Split-Pane Tree & Workbench Tabs (src/pane.c & src/workbench.c)        */
/* ========================================================================= */

typedef enum {
    KNOX_PANE_GRAPH = 0,
    KNOX_PANE_COMMIT_DETAIL,
    KNOX_PANE_VIEWER
} knox_pane_content_t;

typedef enum {
    KNOX_NODE_LEAF = 0,
    KNOX_NODE_SPLIT_V, /* Left / Right */
    KNOX_NODE_SPLIT_H  /* Top / Bottom */
} knox_node_type_t;

typedef struct knox_pane {
    knox_pane_content_t content;
    size_t              repo_idx;
    /* Graph view state */
    size_t              graph_cursor;
    size_t              graph_top;
    bool                graph_all_branches;
    bool                graph_show_remotes;
    /* Commit detail inspector state */
    size_t              inspect_commit_idx;
    size_t              inspect_file_cursor;
    size_t              inspect_file_top;
    /* File or diff viewer document */
    knox_viewer_doc_t   doc;
    /* Layout geometry */
    int                 x;
    int                 y;
    int                 w;
    int                 h;
    uint64_t            id;
} knox_pane_t;

typedef struct knox_split_node {
    knox_node_type_t        type;
    knox_pane_t            *pane; /* Non-NULL for KNOX_NODE_LEAF */
    struct knox_split_node *child1;
    struct knox_split_node *child2;
    struct knox_split_node *parent;
    float                   ratio;
    int                     x;
    int                     y;
    int                     w;
    int                     h;
} knox_split_node_t;

typedef struct {
    char              title[64];
    knox_split_node_t *root;
    knox_pane_t       *active_pane;
} knox_tab_t;

knox_pane_t       *knox_pane_new(knox_pane_content_t content, size_t repo_idx);
void               knox_pane_free(knox_pane_t *p);
knox_split_node_t *knox_split_create_leaf(knox_pane_t *p);
knox_split_node_t *knox_split_pane(knox_split_node_t *target,
                                   knox_node_type_t split_type,
                                   knox_pane_t *new_pane);
knox_pane_t       *knox_split_close(knox_split_node_t *root,
                                    knox_split_node_t *target,
                                    knox_split_node_t **new_root);
void               knox_split_layout(knox_split_node_t *node,
                                     int x, int y, int w, int h);
knox_pane_t       *knox_split_next_pane(knox_split_node_t *root,
                                        knox_pane_t *cur);
knox_split_node_t *knox_split_find_pane(knox_split_node_t *node,
                                        knox_pane_t *p);
size_t             knox_split_count_leaves(const knox_split_node_t *node);
void               knox_split_free_tree(knox_split_node_t *node);

/* ========================================================================= */
/* 7. User Keymap Configuration via libtam tam_ini (src/keymap.c)            */
/* ========================================================================= */

typedef struct {
    tam_key_type_t type;
    uint32_t       codepoint;
    unsigned       mods;
    char           key_label[32]; /* Formatted physical key name, e.g. "Alt+A" */
    char           widget[48];    /* Canonical widget name or "unbind" */
} knox_keybind_t;

typedef struct {
    knox_keybind_t *entries;
    size_t          count;
    size_t          cap;
    char            source_path[512];
    size_t          warning_count;
    char            last_warning[256];
} knox_keymap_t;

void        knox_keymap_init(knox_keymap_t *km);
void        knox_keymap_free(knox_keymap_t *km);
bool        knox_keymap_parse_key(const char *spec, tam_key_type_t *out_type,
                                  uint32_t *out_cp, unsigned *out_mods);
void        knox_keymap_format_key(tam_key_type_t type, uint32_t cp,
                                   unsigned mods, char *out, size_t cap);
bool        knox_keymap_is_valid_widget(const char *widget);
int         knox_keymap_load_string(knox_keymap_t *km, const char *ini_text);
int         knox_keymap_load_file(knox_keymap_t *km, const char *path);
int         knox_keymap_load_default(knox_keymap_t *km);
const char *knox_keymap_lookup(const knox_keymap_t *km,
                               const tam_key_event_t *ev);
bool        knox_keymap_find_widget_key(const knox_keymap_t *km,
                                        const char *widget,
                                        char *out, size_t cap);

/* Top-level Workbench options & entry point */
typedef struct {
    bool ascii;
    bool color;
    bool linux_vt;
} knox_options_t;

int knox_run_workbench(const char *workspace_dir, knox_options_t opts);
int knox_print_status_table(const char *workspace_dir, knox_options_t opts);
int knox_print_repo_graph(const char *repo_dir, size_t max_commits,
                          knox_options_t opts);

#endif /* KNOX_H */
