#include "knox.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s)
{
    size_t l = strlen(s);
    char *c = malloc(l + 1);
    assert(c != NULL);
    memcpy(c, s, l + 1);
    return c;
}

static void add_mock_commit(knox_graph_t *g, const char *hash,
                            const char *p1, const char *p2,
                            const char *subj, bool is_head, bool is_uncomm)
{
    g->commits = realloc(g->commits, (g->commit_count + 1) * sizeof(knox_commit_t));
    assert(g->commits != NULL);
    knox_commit_t *c = &g->commits[g->commit_count++];
    *c = (knox_commit_t){0};
    snprintf(c->hash, sizeof(c->hash), "%s", hash);
    if (p1)
        snprintf(c->parents[c->parent_count++], 41, "%s", p1);
    if (p2)
        snprintf(c->parents[c->parent_count++], 41, "%s", p2);
    c->author = xstrdup("Fred");
    c->email = xstrdup("fred@example.com");
    c->subject = xstrdup(subj);
    c->is_head = is_head;
    c->is_uncommitted = is_uncomm;
}

static void test_linear_and_uncommitted(void)
{
    knox_graph_t g;
    knox_graph_init(&g);
    add_mock_commit(&g, "*",   "c3", NULL, "Uncommitted Changes (2)", false, true);
    add_mock_commit(&g, "c3",  "c2", NULL, "Third commit",            true,  false);
    add_mock_commit(&g, "c2",  "c1", NULL, "Second commit",           false, false);
    add_mock_commit(&g, "c1",  NULL, NULL, "Initial commit",          false, false);

    assert(knox_graph_build_layout(&g) == 0);
    assert(g.vertex_count == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(g.vertices[i].x == 0);
        assert(g.vertices[i].branch_id == 0);
    }

    assert(knox_graph_rasterize(&g, true) == 0);
    char buf[128];
    int vis = 0;
    knox_graph_format_row(&g, 0, 10, false, false, buf, sizeof(buf), &vis);
    assert(buf[0] == 'o');
    knox_graph_format_row(&g, 1, 10, false, false, buf, sizeof(buf), &vis);
    assert(buf[0] == '@');
    knox_graph_format_row(&g, 2, 10, false, false, buf, sizeof(buf), &vis);
    assert(buf[0] == '*');

    knox_graph_free(&g);
}

static void test_branch_and_merge_layout(void)
{
    /*
     * Topology (top to bottom):
     *   0: m1 (parents: c2, f1)  -- Merge commit on main
     *   1: f1 (parents: c1)      -- Feature branch commit
     *   2: c2 (parents: c1)      -- Mainline commit
     *   3: c1 (parents: NULL)    -- Root commit
     */
    knox_graph_t g;
    knox_graph_init(&g);
    add_mock_commit(&g, "m1", "c2", "f1", "Merge feature into dev", true,  false);
    add_mock_commit(&g, "f1", "c1", NULL, "Add feature",            false, false);
    add_mock_commit(&g, "c2", "c1", NULL, "Mainline work",          false, false);
    add_mock_commit(&g, "c1", NULL, NULL, "Root commit",            false, false);

    assert(knox_graph_build_layout(&g) == 0);
    assert(g.vertex_count == 4);
    /* m1, c2, c1 sit on trunk lane 0; f1 sits on parallel lane 1 */
    assert(g.vertices[0].x == 0);
    assert(g.vertices[2].x == 0);
    assert(g.vertices[3].x == 0);
    assert(g.vertices[1].x == 1);
    assert(g.max_lane_x >= 2);

    assert(knox_graph_rasterize(&g, false) == 0);
    char row0[128], row1[128];
    int vis = 0;
    knox_graph_format_row(&g, 0, 16, false, false, row0, sizeof(row0), &vis);
    knox_graph_format_row(&g, 1, 16, false, false, row1, sizeof(row1), &vis);
    assert(strstr(row0, "\xe2\x97\x8e") != NULL); /* ◎ HEAD at m1 */
    assert(strstr(row1, "\xe2\x97\x8f") != NULL); /* ● at f1 */

    knox_graph_free(&g);
}

int main(void)
{
    test_linear_and_uncommitted();
    test_branch_and_merge_layout();
    printf("test-graph: all tests passed\n");
    return 0;
}
