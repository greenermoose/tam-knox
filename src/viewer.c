#include "knox.h"
#include "render_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static bool has_suffix(const char *str, const char *ext)
{
    size_t slen = strlen(str);
    size_t elen = strlen(ext);
    return slen >= elen && strcasecmp(str + slen - elen, ext) == 0;
}

static enum view_format detect_markup_format(const char *path)
{
    if (!path)
        return VIEW_FORMAT_RAW;
    if (has_suffix(path, ".md") || has_suffix(path, ".markdown"))
        return VIEW_FORMAT_MARKDOWN;
    if (has_suffix(path, ".html") || has_suffix(path, ".htm"))
        return VIEW_FORMAT_HTML;
    if (has_suffix(path, ".mmd") || has_suffix(path, ".mermaid"))
        return VIEW_FORMAT_MERMAID;
    return VIEW_FORMAT_RAW;
}

static int index_text_lines(const char *text, size_t len,
                            size_t **out_offsets, size_t **out_lengths,
                            size_t *out_count)
{
    size_t cap = 64;
    size_t *offs = malloc(cap * sizeof(size_t));
    size_t *lens = malloc(cap * sizeof(size_t));
    if (!offs || !lens) {
        free(offs);
        free(lens);
        return -1;
    }

    size_t count = 0;
    size_t pos = 0;
    while (pos <= len) {
        const char *nl = memchr(text + pos, '\n', len - pos);
        size_t l = nl ? (size_t)(nl - (text + pos)) : (len - pos);
        if (l > 0 && text[pos + l - 1] == '\r')
            l--;

        if (count == cap) {
            size_t next = cap * 2;
            size_t *go = realloc(offs, next * sizeof(size_t));
            size_t *gl = realloc(lens, next * sizeof(size_t));
            if (!go || !gl) {
                free(go ? go : offs);
                free(gl ? gl : lens);
                return -1;
            }
            offs = go;
            lens = gl;
            cap = next;
        }
        offs[count] = pos;
        lens[count] = l;
        count++;

        if (!nl)
            break;
        pos = (size_t)(nl - text) + 1;
        if (pos == len)
            break;
    }

    *out_offsets = offs;
    *out_lengths = lens;
    *out_count = count;
    return 0;
}

static void compute_lexer_states(knox_viewer_doc_t *doc)
{
    free(doc->line_states);
    doc->line_states = NULL;
    if (doc->line_count == 0)
        return;

    doc->line_states = calloc(doc->line_count, sizeof(uint8_t));
    if (!doc->line_states)
        return;

    uint8_t scratch[4096];
    knox_lex_state_t st = KNOX_LEX_NORMAL;
    for (size_t i = 0; i < doc->line_count; i++) {
        doc->line_states[i] = (uint8_t)st;
        size_t l = doc->line_lengths[i];
        if (l > sizeof(scratch))
            l = sizeof(scratch);
        st = knox_syntax_highlight_line(doc->lang, st,
                                        doc->raw_text + doc->line_offsets[i],
                                        l, scratch);
    }
}

void knox_viewer_free(knox_viewer_doc_t *doc)
{
    if (!doc)
        return;
    free(doc->title);
    free(doc->repo_name);
    free(doc->abs_path);
    free(doc->breadcrumb);
    free(doc->raw_text);
    free(doc->line_offsets);
    free(doc->line_lengths);
    free(doc->line_states);
    rbuf_free(&doc->rendered_buf);
    free(doc->rend_offsets);
    free(doc->rend_lengths);
    free(doc->sbs_rows);
    *doc = (knox_viewer_doc_t){0};
}

static bool sbs_push_row(knox_sbs_row_t **rows, size_t *count, size_t *cap,
                         knox_sbs_row_t row)
{
    if (*count == *cap) {
        size_t next = *cap ? (*cap * 2) : 64;
        knox_sbs_row_t *grown = realloc(*rows, next * sizeof(*grown));
        if (!grown)
            return false;
        *rows = grown;
        *cap = next;
    }
    (*rows)[(*count)++] = row;
    return true;
}

static bool parse_hunk_header(const char *line, size_t len,
                              int *out_old_start, int *out_new_start)
{
    if (len < 4 || line[0] != '@' || line[1] != '@')
        return false;
    char buf[128];
    size_t clen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, line, clen);
    buf[clen] = '\0';

    const char *minus = strchr(buf + 2, '-');
    const char *plus = minus ? strchr(minus, '+') : NULL;
    if (!minus || !plus)
        return false;
    *out_old_start = atoi(minus + 1);
    *out_new_start = atoi(plus + 1);
    return true;
}

int knox_viewer_build_sbs(knox_viewer_doc_t *doc)
{
    if (!doc || doc->kind != KNOX_DOC_DIFF || !doc->raw_text)
        return -1;
    if (doc->sbs_rows && doc->sbs_count > 0)
        return 0;

    free(doc->sbs_rows);
    doc->sbs_rows = NULL;
    doc->sbs_count = 0;

    size_t cap = 0;
    int old_ln = 0;
    int new_ln = 0;
    bool in_hunk = false;

    size_t i = 0;
    while (i < doc->line_count) {
        size_t off = doc->line_offsets[i];
        size_t len = doc->line_lengths[i];
        const char *line = doc->raw_text + off;

        /* 1. Hunk header: @@ -old,cnt +new,cnt @@ */
        int h_old = 0, h_new = 0;
        if (parse_hunk_header(line, len, &h_old, &h_new)) {
            old_ln = h_old;
            new_ln = h_new;
            in_hunk = true;
            sbs_push_row(&doc->sbs_rows, &doc->sbs_count, &cap,
                         (knox_sbs_row_t){
                             .kind = KNOX_SBS_HUNK,
                             .old_off = off,
                             .old_len = len,
                             .new_off = off,
                             .new_len = len
                         });
            i++;
            continue;
        }

        /* 2. File / commit headers before hunks or between files */
        if (!in_hunk ||
            (len >= 4 && (strncmp(line, "diff ", 5) == 0 ||
                          strncmp(line, "--- ", 4) == 0 ||
                          strncmp(line, "+++ ", 4) == 0 ||
                          strncmp(line, "index ", 6) == 0))) {
            if (len >= 5 && strncmp(line, "diff ", 5) == 0)
                in_hunk = false;
            sbs_push_row(&doc->sbs_rows, &doc->sbs_count, &cap,
                         (knox_sbs_row_t){
                             .kind = KNOX_SBS_HEADER,
                             .old_off = off,
                             .old_len = len,
                             .new_off = off,
                             .new_len = len
                         });
            i++;
            continue;
        }

        /* 3. Paired deletion (-) and addition (+) block inside a hunk */
        if (len > 0 && (line[0] == '-' || line[0] == '+')) {
            size_t del_start = i;
            while (i < doc->line_count) {
                size_t lo = doc->line_offsets[i];
                size_t ll = doc->line_lengths[i];
                const char *lp = doc->raw_text + lo;
                if (ll > 0 && lp[0] == '-' &&
                    !(ll >= 4 && strncmp(lp, "--- ", 4) == 0))
                    i++;
                else
                    break;
            }
            size_t del_end = i;

            size_t add_start = i;
            while (i < doc->line_count) {
                size_t lo = doc->line_offsets[i];
                size_t ll = doc->line_lengths[i];
                const char *lp = doc->raw_text + lo;
                if (ll > 0 && lp[0] == '+' &&
                    !(ll >= 4 && strncmp(lp, "+++ ", 4) == 0))
                    i++;
                else
                    break;
            }
            size_t add_end = i;

            size_t del_cnt = del_end - del_start;
            size_t add_cnt = add_end - add_start;
            size_t pairs = del_cnt > add_cnt ? del_cnt : add_cnt;

            for (size_t k = 0; k < pairs; k++) {
                knox_sbs_row_t row = { .kind = KNOX_SBS_CHANGE };
                if (k < del_cnt) {
                    size_t d_idx = del_start + k;
                    row.old_lineno = old_ln++;
                    row.old_off = doc->line_offsets[d_idx] + 1;
                    row.old_len = doc->line_lengths[d_idx] > 0
                                  ? doc->line_lengths[d_idx] - 1 : 0;
                    row.old_changed = true;
                }
                if (k < add_cnt) {
                    size_t a_idx = add_start + k;
                    row.new_lineno = new_ln++;
                    row.new_off = doc->line_offsets[a_idx] + 1;
                    row.new_len = doc->line_lengths[a_idx] > 0
                                  ? doc->line_lengths[a_idx] - 1 : 0;
                    row.new_changed = true;
                }
                sbs_push_row(&doc->sbs_rows, &doc->sbs_count, &cap, row);
            }
            continue;
        }

        /* 4. Unchanged context line inside a hunk (starts with ' ' or empty) */
        if (len > 0 && line[0] == '\\') {
            /* "\ No newline at end of file" */
            i++;
            continue;
        }
        size_t body_off = (len > 0 && line[0] == ' ') ? off + 1 : off;
        size_t body_len = (len > 0 && line[0] == ' ') ? len - 1 : len;
        sbs_push_row(&doc->sbs_rows, &doc->sbs_count, &cap,
                     (knox_sbs_row_t){
                         .kind = KNOX_SBS_CONTEXT,
                         .old_lineno = old_ln++,
                         .new_lineno = new_ln++,
                         .old_off = body_off,
                         .old_len = body_len,
                         .new_off = body_off,
                         .new_len = body_len
                     });
        i++;
    }

    return 0;
}

int knox_viewer_toggle_side_by_side(knox_viewer_doc_t *doc)
{
    if (!doc || doc->kind != KNOX_DOC_DIFF)
        return -1;
    if (!doc->side_by_side) {
        if (knox_viewer_build_sbs(doc) != 0)
            return -1;
        doc->side_by_side = true;
    } else {
        doc->side_by_side = false;
    }
    doc->top_line = 0;
    doc->cursor_line = 0;
    doc->left_col = 0;
    return 0;
}

int knox_viewer_ensure_rendered(knox_viewer_doc_t *doc, int width, bool ascii)
{
    if (!doc || doc->markup_fmt == VIEW_FORMAT_RAW)
        return -1;
    if (width < 20)
        width = 20;
    if (doc->rend_line_count > 0 &&
        doc->rendered_width == width &&
        doc->rendered_ascii == ascii)
        return 0;

    rbuf_free(&doc->rendered_buf);
    rbuf_init(&doc->rendered_buf);
    free(doc->rend_offsets);
    free(doc->rend_lengths);
    doc->rend_offsets = NULL;
    doc->rend_lengths = NULL;
    doc->rend_line_count = 0;

    if (doc->markup_fmt == VIEW_FORMAT_MARKDOWN) {
        render_markdown(&doc->rendered_buf, doc->raw_text, doc->raw_len,
                        width, ascii);
    } else if (doc->markup_fmt == VIEW_FORMAT_HTML) {
        render_html(&doc->rendered_buf, doc->raw_text, doc->raw_len,
                    width, ascii);
    } else if (doc->markup_fmt == VIEW_FORMAT_MERMAID) {
        render_mermaid(&doc->rendered_buf, doc->raw_text, doc->raw_len,
                       width, ascii);
    }

    const char *rdata = doc->rendered_buf.data ? doc->rendered_buf.data : "";
    size_t rlen = doc->rendered_buf.len;
    if (index_text_lines(rdata, rlen, &doc->rend_offsets, &doc->rend_lengths,
                         &doc->rend_line_count) != 0)
        return -1;

    doc->rendered_width = width;
    doc->rendered_ascii = ascii;
    return 0;
}

int knox_viewer_open_buffer(knox_viewer_doc_t *doc, knox_doc_kind_t kind,
                            const char *title, const char *repo_name,
                            const char *breadcrumb, const char *text,
                            size_t len, knox_lang_t lang)
{
    if (!doc)
        return -1;
    knox_viewer_free(doc);
    doc->kind = kind;
    doc->title = xstrdup(title ? title : "Untitled");
    doc->repo_name = repo_name ? xstrdup(repo_name) : NULL;
    doc->breadcrumb = xstrdup(breadcrumb ? breadcrumb : doc->title);
    doc->raw_len = len;
    doc->raw_text = malloc(len + 1);
    if (!doc->raw_text)
        return -1;
    if (text && len > 0)
        memcpy(doc->raw_text, text, len);
    doc->raw_text[len] = '\0';

    if (index_text_lines(doc->raw_text, doc->raw_len,
                         &doc->line_offsets, &doc->line_lengths,
                         &doc->line_count) != 0)
        return -1;

    doc->lang = lang;
    doc->markup_fmt = VIEW_FORMAT_RAW;
    doc->render_mode = false;
    doc->focused_link = -1;
    compute_lexer_states(doc);
    return 0;
}

int knox_viewer_open_file(knox_viewer_doc_t *doc, const char *abs_path,
                          const char *repo_name, bool prefer_render,
                          int width, bool ascii)
{
    if (!doc || !abs_path)
        return -1;
    size_t flen = 0;
    char *raw = tam_fs_read(abs_path, 0, &flen);
    if (!raw)
        return -1;

    /* Sanitize NUL bytes if someone opens a binary file */
    for (size_t i = 0; i < flen; i++) {
        if (raw[i] == '\0')
            raw[i] = '.';
    }

    const char *base = strrchr(abs_path, '/');
    base = (base && base[1]) ? base + 1 : abs_path;

    char bc[512];
    if (repo_name && *repo_name)
        snprintf(bc, sizeof(bc), "%s > %s", repo_name, base);
    else
        snprintf(bc, sizeof(bc), "%s", abs_path);

    const char *nl = memchr(raw, '\n', flen);
    size_t first_len = nl ? (size_t)(nl - raw) : flen;
    knox_lang_t lang = knox_syntax_detect(abs_path, raw, first_len);

    int rc = knox_viewer_open_buffer(doc, KNOX_DOC_FILE, base, repo_name,
                                     bc, raw, flen, lang);
    free(raw);
    if (rc != 0)
        return -1;

    doc->abs_path = xstrdup(abs_path);
    doc->markup_fmt = detect_markup_format(abs_path);
    if (prefer_render && doc->markup_fmt != VIEW_FORMAT_RAW) {
        if (knox_viewer_ensure_rendered(doc, width, ascii) == 0)
            doc->render_mode = true;
    }
    return 0;
}

int knox_viewer_toggle_render(knox_viewer_doc_t *doc, int width, bool ascii)
{
    if (!doc)
        return -1;
    if (doc->kind == KNOX_DOC_DIFF)
        return knox_viewer_toggle_side_by_side(doc);
    if (doc->markup_fmt == VIEW_FORMAT_RAW) {
        /* Allow forcing Markdown render on plain text files like README */
        doc->markup_fmt = VIEW_FORMAT_MARKDOWN;
    }
    if (!doc->render_mode) {
        if (knox_viewer_ensure_rendered(doc, width, ascii) != 0)
            return -1;
        doc->render_mode = true;
    } else {
        doc->render_mode = false;
    }
    doc->top_line = 0;
    doc->cursor_line = 0;
    doc->left_col = 0;
    return 0;
}

bool knox_viewer_search(knox_viewer_doc_t *doc, const char *query,
                        bool forward, bool step)
{
    if (!doc)
        return false;
    if (query && *query) {
        snprintf(doc->search_query, sizeof(doc->search_query), "%s", query);
    }
    if (doc->search_query[0] == '\0')
        return false;

    size_t qlen = strlen(doc->search_query);

    if (doc->kind == KNOX_DOC_DIFF && doc->side_by_side &&
        doc->sbs_rows && doc->sbs_count > 0 && doc->raw_text) {
        size_t total = doc->sbs_count;
        size_t start = doc->cursor_line < total ? doc->cursor_line : 0;
        for (size_t k = (step ? 1 : 0); k <= total; k++) {
            size_t idx = forward
                         ? (start + k) % total
                         : (start + total - (k % total)) % total;
            const knox_sbs_row_t *r = &doc->sbs_rows[idx];
            bool hit_old = (r->old_len >= qlen &&
                            memmem(doc->raw_text + r->old_off, r->old_len,
                                   doc->search_query, qlen) != NULL);
            bool hit_new = (r->new_len >= qlen &&
                            memmem(doc->raw_text + r->new_off, r->new_len,
                                   doc->search_query, qlen) != NULL);
            if (hit_old || hit_new) {
                doc->cursor_line = idx;
                return true;
            }
        }
        return false;
    }

    const char *text = doc->render_mode && doc->rendered_buf.data
                       ? doc->rendered_buf.data : doc->raw_text;
    const size_t *offs = doc->render_mode ? doc->rend_offsets : doc->line_offsets;
    const size_t *lens = doc->render_mode ? doc->rend_lengths : doc->line_lengths;
    size_t total = doc->render_mode ? doc->rend_line_count : doc->line_count;
    if (!text || total == 0)
        return false;

    size_t start = doc->cursor_line;
    if (start >= total)
        start = 0;

    for (size_t k = (step ? 1 : 0); k <= total; k++) {
        size_t idx = forward
                     ? (start + k) % total
                     : (start + total - (k % total)) % total;
        if (lens[idx] >= qlen &&
            memmem(text + offs[idx], lens[idx], doc->search_query, qlen) != NULL) {
            doc->cursor_line = idx;
            return true;
        }
    }
    return false;
}
