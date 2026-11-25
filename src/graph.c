#include "knox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Direct C11 port of mhutchie.git-graph (v1.30.0, media/out.min.js)
 * Vertex (`Pe`), Branch (`De`), and Graph (`Me`: `loadCommits`,
 * `determinePath`, `getAvailableColour`) plus a 2-column-per-lane
 * terminal cell matrix rasterizer.
 */

#define NULL_VERTEX_ID (-1)
#define UNOCCUPIED_CONN (-2)
#define GIT_LOG_SEP "\x1f"

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

void knox_graph_init(knox_graph_t *g)
{
    *g = (knox_graph_t){0};
}

static void free_str_list(char **list, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(list[i]);
    free(list);
}

void knox_graph_free(knox_graph_t *g)
{
    if (!g)
        return;
    for (size_t i = 0; i < g->commit_count; i++) {
        knox_commit_t *c = &g->commits[i];
        free(c->author);
        free(c->email);
        free(c->subject);
        free_str_list(c->heads, c->head_count);
        free_str_list(c->remotes, c->remote_count);
        free_str_list(c->tags, c->tag_count);
    }
    free(g->commits);

    for (size_t i = 0; i < g->vertex_count; i++) {
        free(g->vertices[i].parents);
        free(g->vertices[i].children);
        free(g->vertices[i].connections);
    }
    free(g->vertices);

    for (size_t i = 0; i < g->branch_count; i++)
        free(g->branches[i].lines);
    free(g->branches);

    free(g->available_colours);

    for (size_t i = 0; i < g->row_count; i++)
        free(g->rows[i].cells);
    free(g->rows);

    *g = (knox_graph_t){0};
}

static void str_list_push(char ***list, size_t *count, const char *val)
{
    char **grown = realloc(*list, (*count + 1) * sizeof(char *));
    if (!grown)
        return;
    grown[*count] = xstrdup(val);
    *list = grown;
    (*count)++;
}

static ssize_t find_commit_index(const knox_graph_t *g, const char *hash)
{
    for (size_t i = 0; i < g->commit_count; i++) {
        if (strcmp(g->commits[i].hash, hash) == 0)
            return (ssize_t)i;
    }
    return -1;
}

/* ========================================================================= */
/* Vertex & Branch primitives (matching mhutchie.git-graph media/out.min.js) */
/* ========================================================================= */

static void vertex_add_parent(knox_vertex_t *v, int parent_id)
{
    if (v->parent_count == v->parent_cap) {
        size_t next = v->parent_cap ? v->parent_cap * 2 : 4;
        int *grown = realloc(v->parents, next * sizeof(int));
        if (!grown)
            return;
        v->parents = grown;
        v->parent_cap = next;
    }
    v->parents[v->parent_count++] = parent_id;
}

static void vertex_add_child(knox_vertex_t *v, int child_id)
{
    if (v->child_count == v->child_cap) {
        size_t next = v->child_cap ? v->child_cap * 2 : 4;
        int *grown = realloc(v->children, next * sizeof(int));
        if (!grown)
            return;
        v->children = grown;
        v->child_cap = next;
    }
    v->children[v->child_count++] = child_id;
}

static bool vertex_has_next_parent(const knox_vertex_t *v)
{
    return v->next_parent < v->parent_count;
}

static int vertex_get_next_parent(const knox_vertex_t *v)
{
    if (v->next_parent < v->parent_count)
        return v->parents[v->next_parent];
    return -2; /* None */
}

static void vertex_register_parent_processed(knox_vertex_t *v)
{
    v->next_parent++;
}

static bool vertex_is_merge(const knox_vertex_t *v)
{
    return v->parent_count > 1;
}

static void vertex_add_to_branch(knox_vertex_t *v, int branch_id, int x)
{
    if (v->branch_id == -1) {
        v->branch_id = branch_id;
        v->x = x;
    }
}

static bool vertex_is_not_on_branch(const knox_vertex_t *v)
{
    return v->branch_id == -1;
}

static knox_point_t vertex_get_point(const knox_vertex_t *v)
{
    return (knox_point_t){ .x = v->x, .y = v->id };
}

static knox_point_t vertex_get_next_point(const knox_vertex_t *v)
{
    return (knox_point_t){ .x = v->next_x, .y = v->id };
}

static bool vertex_get_point_connecting_to(const knox_vertex_t *v,
                                           int connects_to, int branch_id,
                                           knox_point_t *out_pt)
{
    for (size_t i = 0; i < v->conn_cap; i++) {
        if (v->connections[i].connects_to == connects_to &&
            v->connections[i].branch_id == branch_id) {
            *out_pt = (knox_point_t){ .x = (int)i, .y = v->id };
            return true;
        }
    }
    return false;
}

static void vertex_register_unavailable_point(knox_vertex_t *v, int x,
                                              int connects_to, int branch_id)
{
    if (x == v->next_x) {
        v->next_x = x + 1;
        if ((size_t)(x + 1) > v->conn_cap) {
            size_t next = v->conn_cap ? v->conn_cap * 2 : 8;
            while (next < (size_t)(x + 1))
                next *= 2;
            knox_conn_t *grown = realloc(v->connections,
                                         next * sizeof(knox_conn_t));
            if (!grown)
                return;
            for (size_t k = v->conn_cap; k < next; k++) {
                grown[k].connects_to = UNOCCUPIED_CONN;
                grown[k].branch_id = -1;
            }
            v->connections = grown;
            v->conn_cap = next;
        }
        v->connections[x].connects_to = connects_to;
        v->connections[x].branch_id = branch_id;
    }
}

static int graph_new_branch(knox_graph_t *g, int colour)
{
    if (g->branch_count == g->branch_cap) {
        size_t next = g->branch_cap ? g->branch_cap * 2 : 16;
        knox_branch_t *grown = realloc(g->branches,
                                       next * sizeof(knox_branch_t));
        if (!grown)
            return 0;
        g->branches = grown;
        g->branch_cap = next;
    }
    int id = (int)g->branch_count++;
    g->branches[id] = (knox_branch_t){
        .colour = colour,
        .end_y = 0,
        .num_uncommitted = 0,
        .lines = NULL,
        .line_count = 0,
        .line_cap = 0
    };
    return id;
}

static void branch_add_line(knox_branch_t *b, knox_point_t p1, knox_point_t p2,
                            bool is_committed, bool locked_first)
{
    if (b->line_count == b->line_cap) {
        size_t next = b->line_cap ? b->line_cap * 2 : 16;
        knox_line_seg_t *grown = realloc(b->lines,
                                         next * sizeof(knox_line_seg_t));
        if (!grown)
            return;
        b->lines = grown;
        b->line_cap = next;
    }
    b->lines[b->line_count++] = (knox_line_seg_t){
        .p1 = p1,
        .p2 = p2,
        .is_committed = is_committed,
        .locked_first = locked_first
    };
}

static int graph_get_available_colour(knox_graph_t *g, int start_y)
{
    for (size_t i = 0; i < g->avail_colour_count; i++) {
        if (start_y > g->available_colours[i])
            return (int)i;
    }
    if (g->avail_colour_count == g->avail_colour_cap) {
        size_t next = g->avail_colour_cap ? g->avail_colour_cap * 2 : 16;
        int *grown = realloc(g->available_colours, next * sizeof(int));
        if (!grown)
            return 0;
        g->available_colours = grown;
        g->avail_colour_cap = next;
    }
    int idx = (int)g->avail_colour_count++;
    g->available_colours[idx] = 0;
    return idx;
}

static void graph_determine_path(knox_graph_t *g, size_t start_at)
{
    size_t t = start_at;
    knox_vertex_t *i = &g->vertices[t];
    int parent_id = vertex_get_next_parent(i);
    knox_point_t s = vertex_is_not_on_branch(i)
                     ? vertex_get_next_point(i)
                     : vertex_get_point(i);

    if (parent_id >= 0 &&
        vertex_is_merge(i) &&
        !vertex_is_not_on_branch(i) &&
        !vertex_is_not_on_branch(&g->vertices[parent_id])) {
        /* Merge edge into an already-placed parent branch */
        bool found_point_to_parent = false;
        knox_vertex_t *parent_v = &g->vertices[parent_id];
        int parent_branch_id = parent_v->branch_id;
        for (t = start_at + 1; t < g->vertex_count; t++) {
            knox_vertex_t *o = &g->vertices[t];
            knox_point_t r;
            if (vertex_get_point_connecting_to(o, parent_id, parent_branch_id, &r)) {
                found_point_to_parent = true;
            } else {
                r = vertex_get_next_point(o);
            }
            branch_add_line(&g->branches[parent_branch_id], s, r,
                            i->is_committed,
                            (!found_point_to_parent && (int)t != parent_id)
                            ? (s.x < r.x) : true);
            vertex_register_unavailable_point(o, r.x, parent_id, parent_branch_id);
            s = r;
            if (found_point_to_parent) {
                vertex_register_parent_processed(i);
                break;
            }
        }
    } else {
        /* Normal branch path */
        int colour = graph_get_available_colour(g, (int)start_at);
        int branch_id = graph_new_branch(g, colour);
        vertex_add_to_branch(i, branch_id, s.x);
        vertex_register_unavailable_point(i, s.x, i->id, branch_id);

        for (t = start_at + 1; t < g->vertex_count; t++) {
            knox_vertex_t *o = &g->vertices[t];
            knox_point_t r;
            if (parent_id == (int)t && !vertex_is_not_on_branch(o))
                r = vertex_get_point(o);
            else
                r = vertex_get_next_point(o);

            branch_add_line(&g->branches[branch_id], s, r,
                            i->is_committed, s.x < r.x);
            vertex_register_unavailable_point(o, r.x, parent_id, branch_id);
            s = r;

            if (parent_id == (int)t) {
                vertex_register_parent_processed(i);
                bool parent_already_on_branch = !vertex_is_not_on_branch(o);
                vertex_add_to_branch(o, branch_id, r.x);
                i = o;
                parent_id = vertex_get_next_parent(i);
                if (parent_id == -2 || parent_already_on_branch)
                    break;
            }
        }
        if (t == g->vertex_count && parent_id == NULL_VERTEX_ID)
            vertex_register_parent_processed(i);

        g->branches[branch_id].end_y = (int)t;
        g->available_colours[colour] = (int)t;
    }
}

int knox_graph_build_layout(knox_graph_t *g)
{
    if (!g || g->commit_count == 0)
        return 0;

    g->vertex_count = g->commit_count;
    g->vertices = calloc(g->vertex_count, sizeof(knox_vertex_t));
    if (!g->vertices)
        return -1;

    for (size_t s = 0; s < g->commit_count; s++) {
        knox_vertex_t *v = &g->vertices[s];
        v->id = (int)s;
        v->x = 0;
        v->next_x = 0;
        v->branch_id = -1;
        v->is_committed = !g->commits[s].is_uncommitted;
        v->is_current = g->commits[s].is_head;
        v->is_stash = g->commits[s].is_stash;
    }

    for (size_t s = 0; s < g->commit_count; s++) {
        const knox_commit_t *c = &g->commits[s];
        for (size_t r = 0; r < c->parent_count; r++) {
            ssize_t pidx = find_commit_index(g, c->parents[r]);
            if (pidx >= 0) {
                vertex_add_parent(&g->vertices[s], (int)pidx);
                vertex_add_child(&g->vertices[pidx], (int)s);
            } else {
                vertex_add_parent(&g->vertices[s], NULL_VERTEX_ID);
            }
        }
    }

    size_t s = 0;
    while (s < g->vertex_count) {
        if (vertex_has_next_parent(&g->vertices[s]) ||
            vertex_is_not_on_branch(&g->vertices[s])) {
            graph_determine_path(g, s);
        } else {
            s++;
        }
    }

    int max_x = 0;
    for (size_t i = 0; i < g->vertex_count; i++) {
        if (g->vertices[i].next_x > max_x)
            max_x = g->vertices[i].next_x;
        if (g->vertices[i].x + 1 > max_x)
            max_x = g->vertices[i].x + 1;
    }
    g->max_lane_x = max_x > 0 ? max_x : 1;
    return 0;
}

/* ========================================================================= */
/* Terminal Cell Matrix Rasterizer                                           */
/* ========================================================================= */

static void set_cell(knox_graph_row_t *row, int col, const char *glyph,
                     uint8_t colour, bool is_node, bool is_uncommitted)
{
    if (col < 0 || col >= row->width)
        return;
    knox_graph_cell_t *cell = &row->cells[col];
    if (cell->is_node && !is_node)
        return; /* Never overwrite a commit vertex node with a line */

    /* Combine vertical and horizontal junctions cleanly */
    if (!is_node && strcmp(cell->ch, " ") != 0) {
        bool old_v = (strcmp(cell->ch, "\xe2\x94\x82") == 0 ||
                      strcmp(cell->ch, "|") == 0);
        bool old_h = (strcmp(cell->ch, "\xe2\x94\x80") == 0 ||
                      strcmp(cell->ch, "-") == 0);
        bool new_v = (strcmp(glyph, "\xe2\x94\x82") == 0 ||
                      strcmp(glyph, "|") == 0);
        bool new_h = (strcmp(glyph, "\xe2\x94\x80") == 0 ||
                      strcmp(glyph, "-") == 0);
        if ((old_v && new_h) || (old_h && new_v)) {
            bool ascii = (strcmp(glyph, "|") == 0 || strcmp(glyph, "-") == 0 ||
                          strcmp(cell->ch, "|") == 0 || strcmp(cell->ch, "-") == 0);
            snprintf(cell->ch, sizeof(cell->ch), "%s",
                     ascii ? "+" : "\xe2\x94\xbc");
            return;
        }
    }

    snprintf(cell->ch, sizeof(cell->ch), "%s", glyph);
    cell->colour = colour;
    cell->is_node = is_node;
    cell->is_uncommitted = is_uncommitted;
}

int knox_graph_rasterize(knox_graph_t *g, bool ascii)
{
    if (!g || g->vertex_count == 0)
        return 0;

    int cell_width = g->max_lane_x * 2;
    if (cell_width < 2)
        cell_width = 2;
    if (cell_width > 40)
        cell_width = 40;

    g->row_count = g->vertex_count;
    g->rows = calloc(g->row_count, sizeof(knox_graph_row_t));
    if (!g->rows)
        return -1;

    for (size_t y = 0; y < g->row_count; y++) {
        g->rows[y].width = cell_width;
        g->rows[y].cells = calloc((size_t)cell_width, sizeof(knox_graph_cell_t));
        if (!g->rows[y].cells)
            return -1;
        for (int c = 0; c < cell_width; c++)
            snprintf(g->rows[y].cells[c].ch, sizeof(g->rows[y].cells[c].ch), " ");
    }

    const char *glyph_v  = ascii ? "|" : "\xe2\x94\x82"; /* │ */
    const char *glyph_h  = ascii ? "-" : "\xe2\x94\x80"; /* ─ */
    const char *glyph_tr = ascii ? "\\" : "\xe2\x95\xae"; /* ╮ */
    const char *glyph_tl = ascii ? "/" : "\xe2\x95\xad"; /* ╭ */
    const char *glyph_bl = ascii ? "\\" : "\xe2\x95\xb0"; /* ╰ */
    const char *glyph_br = ascii ? "/" : "\xe2\x95\xaf"; /* ╯ */

    /* 1. Rasterize branch line segments */
    for (size_t b = 0; b < g->branch_count; b++) {
        const knox_branch_t *br = &g->branches[b];
        uint8_t col_idx = (uint8_t)(br->colour % 6);
        for (size_t l = 0; l < br->line_count; l++) {
            const knox_line_seg_t *seg = &br->lines[l];
            int y1 = seg->p1.y;
            int y2 = seg->p2.y;
            int x1 = seg->p1.x;
            int x2 = seg->p2.x;
            bool uncomm = !seg->is_committed;
            if (y1 < 0 || (size_t)y1 >= g->row_count)
                continue;

            if (x1 == x2) {
                set_cell(&g->rows[y1], 2 * x1, glyph_v, col_idx, false, uncomm);
                if (y2 >= 0 && (size_t)y2 < g->row_count)
                    set_cell(&g->rows[y2], 2 * x2, glyph_v, col_idx, false, uncomm);
            } else if (x2 > x1) {
                /* Branching / shifting right from x1 to x2 */
                int row_shift = seg->locked_first ? y1 : (y2 < (int)g->row_count ? y2 : y1);
                for (int c = 2 * x1 + 1; c < 2 * x2; c++)
                    set_cell(&g->rows[row_shift], c, glyph_h, col_idx, false, uncomm);
                if (seg->locked_first) {
                    set_cell(&g->rows[y1], 2 * x2, glyph_tr, col_idx, false, uncomm);
                    if (y2 >= 0 && (size_t)y2 < g->row_count)
                        set_cell(&g->rows[y2], 2 * x2, glyph_v, col_idx, false, uncomm);
                } else if (y2 >= 0 && (size_t)y2 < g->row_count) {
                    set_cell(&g->rows[y1], 2 * x1, glyph_v, col_idx, false, uncomm);
                    set_cell(&g->rows[y2], 2 * x1, glyph_bl, col_idx, false, uncomm);
                }
            } else {
                /* Merging / shifting left from x1 to x2 (x2 < x1) */
                int row_shift = (y2 >= 0 && (size_t)y2 < g->row_count) ? y2 : y1;
                set_cell(&g->rows[y1], 2 * x1, glyph_v, col_idx, false, uncomm);
                for (int c = 2 * x2 + 1; c < 2 * x1; c++)
                    set_cell(&g->rows[row_shift], c, glyph_h, col_idx, false, uncomm);
                set_cell(&g->rows[row_shift], 2 * x1, glyph_br, col_idx, false, uncomm);
                if (row_shift == y1)
                    set_cell(&g->rows[row_shift], 2 * x2, glyph_tl, col_idx, false, uncomm);
            }
        }
    }

    /* 2. Place commit vertex nodes on top */
    for (size_t y = 0; y < g->vertex_count; y++) {
        const knox_vertex_t *v = &g->vertices[y];
        uint8_t col_idx = 0;
        if (v->branch_id >= 0 && (size_t)v->branch_id < g->branch_count)
            col_idx = (uint8_t)(g->branches[v->branch_id].colour % 6);

        const char *node_glyph;
        if (!v->is_committed)
            node_glyph = ascii ? "o" : "\xe2\x97\x8c"; /* ◌ */
        else if (v->is_current)
            node_glyph = ascii ? "@" : "\xe2\x97\x8e"; /* ◎ */
        else if (v->is_stash)
            node_glyph = ascii ? "$" : "\xe2\x97\x88"; /* ◈ */
        else if (v->parent_count > 1)
            node_glyph = ascii ? "M" : "\xe2\x97\x86"; /* ◆ */
        else
            node_glyph = ascii ? "*" : "\xe2\x97\x8f"; /* ● */

        set_cell(&g->rows[y], 2 * v->x, node_glyph, col_idx, true, !v->is_committed);
    }

    return 0;
}

static const char *graph_colour_sgr(uint8_t colour, bool is_uncommitted)
{
    if (is_uncommitted)
        return "\033[90m";
    static const char *const PALETTE[6] = {
        "\033[36m", /* Cyan */
        "\033[35m", /* Magenta */
        "\033[32m", /* Green */
        "\033[33m", /* Yellow */
        "\033[34m", /* Blue */
        "\033[31m"  /* Red */
    };
    return PALETTE[colour % 6];
}

size_t knox_graph_format_row(const knox_graph_t *g, size_t row_idx,
                             int max_cell_cols, bool color, bool linux_vt,
                             char *out, size_t out_cap, int *out_vis_cols)
{
    (void)linux_vt;
    if (!out || out_cap == 0)
        return 0;
    out[0] = '\0';
    if (!g || row_idx >= g->row_count) {
        if (out_vis_cols)
            *out_vis_cols = 0;
        return 0;
    }

    const knox_graph_row_t *row = &g->rows[row_idx];
    int cols = row->width;
    if (max_cell_cols > 0 && cols > max_cell_cols)
        cols = max_cell_cols;

    size_t pos = 0;
    for (int c = 0; c < cols; c++) {
        const knox_graph_cell_t *cell = &row->cells[c];
        if (color && strcmp(cell->ch, " ") != 0) {
            const char *sgr = graph_colour_sgr(cell->colour, cell->is_uncommitted);
            int n = snprintf(out + pos, out_cap - pos, "%s%s%s\033[0m",
                             cell->is_node ? "\033[1m" : "",
                             sgr, cell->ch);
            if (n > 0 && pos + (size_t)n < out_cap)
                pos += (size_t)n;
        } else {
            int n = snprintf(out + pos, out_cap - pos, "%s", cell->ch);
            if (n > 0 && pos + (size_t)n < out_cap)
                pos += (size_t)n;
        }
    }
    if (out_vis_cols)
        *out_vis_cols = cols;
    return pos;
}

int knox_repo_load_graph(const knox_repo_t *repo, knox_graph_t *g,
                         size_t max_commits, bool all_branches,
                         bool include_remotes, bool ascii)
{
    if (!repo || !g)
        return -1;
    knox_graph_free(g);
    knox_graph_init(g);
    snprintf(g->head_hash, sizeof(g->head_hash), "%s", repo->head_hash);

    if (max_commits == 0)
        max_commits = 250;

    /* 1. Prepend synthetic Uncommitted Changes node if repo is dirty */
    if (repo->dirty && repo->head_hash[0] != '\0') {
        g->commits = calloc(max_commits + 2, sizeof(knox_commit_t));
        if (!g->commits)
            return -1;
        g->commit_cap = max_commits + 2;
        knox_commit_t *uc = &g->commits[g->commit_count++];
        snprintf(uc->hash, sizeof(uc->hash), "%s", KNOX_UNCOMMITTED_HASH);
        snprintf(uc->parents[0], sizeof(uc->parents[0]), "%s", repo->head_hash);
        uc->parent_count = 1;
        uc->author = xstrdup("*");
        uc->email = xstrdup("");
        uc->timestamp = (int64_t)time(NULL);
        char msg[128];
        snprintf(msg, sizeof(msg), "Uncommitted Changes (%zu)", repo->change_count);
        uc->subject = xstrdup(msg);
        uc->is_uncommitted = true;
    }

    /* 2. Load git log in topological order */
    char max_arg[64];
    snprintf(max_arg, sizeof(max_arg), "--max-count=%zu", max_commits);
    const char *fmt_arg = "--format=%H" GIT_LOG_SEP "%P" GIT_LOG_SEP
                          "%an" GIT_LOG_SEP "%ae" GIT_LOG_SEP "%at" GIT_LOG_SEP "%s";

    const char *argv[16];
    size_t k = 0;
    argv[k++] = "git";
    argv[k++] = "-C";
    argv[k++] = repo->root_path;
    argv[k++] = "-c";
    argv[k++] = "log.showSignature=false";
    argv[k++] = "log";
    argv[k++] = max_arg;
    argv[k++] = fmt_arg;
    argv[k++] = "--topo-order";
    if (all_branches) {
        argv[k++] = "--branches";
        argv[k++] = "--tags";
        if (include_remotes)
            argv[k++] = "--remotes";
    }
    argv[k++] = "HEAD";
    argv[k++] = "--";
    argv[k] = NULL;

    tam_exec_result_t log_res = tam_exec(argv, 15000);
    if (log_res.exit_code == 0 && log_res.stdout_buf) {
        char *line = log_res.stdout_buf;
        while (*line) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = '\0';
            if (*line) {
                char *fields[6] = {0};
                size_t nf = 0;
                char *cur = line;
                while (nf < 5) {
                    char *sep = strchr(cur, '\x1f');
                    if (!sep)
                        break;
                    *sep = '\0';
                    fields[nf++] = cur;
                    cur = sep + 1;
                }
                fields[nf++] = cur;
                if (nf == 6) {
                    if (g->commit_count == g->commit_cap) {
                        size_t next = g->commit_cap ? g->commit_cap * 2 : 64;
                        knox_commit_t *grown = realloc(g->commits,
                                                       next * sizeof(knox_commit_t));
                        if (!grown)
                            break;
                        g->commits = grown;
                        g->commit_cap = next;
                    }
                    knox_commit_t *c = &g->commits[g->commit_count++];
                    *c = (knox_commit_t){0};
                    snprintf(c->hash, sizeof(c->hash), "%.40s", fields[0]);
                    if (strcmp(c->hash, repo->head_hash) == 0)
                        c->is_head = true;

                    /* Parse space-separated parent hashes */
                    char *pstr = fields[1];
                    while (*pstr && c->parent_count < KNOX_MAX_PARENTS) {
                        while (*pstr == ' ')
                            pstr++;
                        if (!*pstr)
                            break;
                        char *sp = strchr(pstr, ' ');
                        if (sp)
                            *sp = '\0';
                        snprintf(c->parents[c->parent_count++], 41, "%.40s", pstr);
                        if (!sp)
                            break;
                        pstr = sp + 1;
                    }
                    c->author = xstrdup(fields[2]);
                    c->email = xstrdup(fields[3]);
                    c->timestamp = strtoll(fields[4], NULL, 10);
                    c->subject = xstrdup(fields[5]);
                }
            }
            if (!nl)
                break;
            line = nl + 1;
        }
    }
    tam_exec_result_free(&log_res);

    /* 3. Attach refs via git show-ref -d --head (matching mhutchie.git-graph) */
    const char *ref_argv[] = {
        "git", "-C", repo->root_path, "show-ref", "-d", "--head", NULL
    };
    tam_exec_result_t ref_res = tam_exec(ref_argv, 15000);
    if (ref_res.exit_code == 0 && ref_res.stdout_buf) {
        char *line = ref_res.stdout_buf;
        while (*line) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = '\0';
            char *sp = strchr(line, ' ');
            if (sp) {
                *sp = '\0';
                const char *hash = line;
                const char *ref = sp + 1;
                ssize_t cidx = find_commit_index(g, hash);
                if (cidx >= 0) {
                    knox_commit_t *c = &g->commits[cidx];
                    if (strncmp(ref, "refs/heads/", 11) == 0) {
                        str_list_push(&c->heads, &c->head_count, ref + 11);
                    } else if (strncmp(ref, "refs/remotes/", 13) == 0) {
                        if (include_remotes &&
                            strncmp(ref + strlen(ref) - 5, "/HEAD", 5) != 0) {
                            str_list_push(&c->remotes, &c->remote_count, ref + 13);
                        }
                    } else if (strncmp(ref, "refs/tags/", 10) == 0) {
                        const char *tname = ref + 10;
                        size_t tlen = strlen(tname);
                        char clean_tag[128];
                        if (tlen > 3 && strcmp(tname + tlen - 3, "^{}") == 0) {
                            snprintf(clean_tag, sizeof(clean_tag), "%.*s",
                                     (int)(tlen - 3), tname);
                        } else {
                            snprintf(clean_tag, sizeof(clean_tag), "%s", tname);
                        }
                        /* Avoid duplicate tag when annotated tag has both object and ^{} */
                        bool dup = false;
                        for (size_t t = 0; t < c->tag_count; t++) {
                            if (strcmp(c->tags[t], clean_tag) == 0)
                                dup = true;
                        }
                        if (!dup)
                            str_list_push(&c->tags, &c->tag_count, clean_tag);
                    }
                }
            }
            if (!nl)
                break;
            line = nl + 1;
        }
    }
    tam_exec_result_free(&ref_res);

    /* 4. Build graph layout and rasterize cells */
    if (knox_graph_build_layout(g) != 0)
        return -1;
    if (knox_graph_rasterize(g, ascii) != 0)
        return -1;

    return 0;
}
