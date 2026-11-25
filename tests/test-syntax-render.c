#include "knox.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_syntax_detection_and_lexing(void)
{
    assert(knox_syntax_detect("src/main.c", NULL, 0) == KNOX_LANG_C);
    assert(knox_syntax_detect("Makefile", NULL, 0) == KNOX_LANG_MAKEFILE);
    assert(knox_syntax_detect("README.md", NULL, 0) == KNOX_LANG_MARKDOWN);
    assert(knox_syntax_detect("index.html", NULL, 0) == KNOX_LANG_HTML);
    assert(knox_syntax_detect("script", "#!/bin/sh\n", 10) == KNOX_LANG_SHELL);
    assert(knox_syntax_detect("patch.diff", NULL, 0) == KNOX_LANG_DIFF);

    const char *c_line = "static int count = 42; /* block";
    uint8_t toks[64];
    knox_lex_state_t st = knox_syntax_highlight_line(
        KNOX_LANG_C, KNOX_LEX_NORMAL, c_line, strlen(c_line), toks);
    assert(toks[0] == KNOX_TOK_KEYWORD); /* static */
    assert(toks[7] == KNOX_TOK_TYPE);    /* int */
    assert(toks[19] == KNOX_TOK_NUMBER); /* 42 */
    assert(st == KNOX_LEX_BLOCK_COMMENT);

    const char *c_line2 = "still comment */ return count;";
    st = knox_syntax_highlight_line(
        KNOX_LANG_C, st, c_line2, strlen(c_line2), toks);
    assert(toks[0] == KNOX_TOK_COMMENT);
    assert(toks[17] == KNOX_TOK_KEYWORD); /* return */
    assert(st == KNOX_LEX_NORMAL);

    const char *diff_add = "+added line";
    knox_syntax_highlight_line(KNOX_LANG_DIFF, KNOX_LEX_NORMAL,
                               diff_add, strlen(diff_add), toks);
    assert(toks[0] == KNOX_TOK_DIFF_ADD);
}

static void test_viewer_markdown_and_html_render(void)
{
    char tmp_dir[] = "/tmp/knox-md-XXXXXX";
    assert(mkdtemp(tmp_dir) != NULL);
    char *tmp_md = tam_path_join(tmp_dir, "README.md");
    assert(tmp_md != NULL);
    const char *md =
        "# Title Heading\n\n"
        "A paragraph with **bold** and `code`.\n\n"
        "| Col A | Col B |\n"
        "| --- | --- |\n"
        "| val 1 | val 2 |\n";
    assert(tam_fs_write_atomic(tmp_md, md, strlen(md), 0644));

    knox_viewer_doc_t doc = {0};
    assert(knox_viewer_open_file(&doc, tmp_md, "test-repo", true, 72, false) == 0);
    assert(doc.render_mode == true);
    assert(doc.rend_line_count >= 5);
    assert(strstr(doc.rendered_buf.data, "Title Heading") != NULL);

    /* Toggle to raw mode and verify raw line count & search */
    assert(knox_viewer_toggle_render(&doc, 72, false) == 0);
    assert(doc.render_mode == false);
    assert(doc.line_count == 7);
    assert(knox_viewer_search(&doc, "Col A", true, false) == true);
    assert(doc.cursor_line == 4);

    knox_viewer_free(&doc);
    unlink(tmp_md);
    rmdir(tmp_dir);
    free(tmp_md);
}

int main(void)
{
    test_syntax_detection_and_lexing();
    test_viewer_markdown_and_html_render();
    printf("test-syntax-render: all tests passed\n");
    return 0;
}
