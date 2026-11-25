#include "knox.h"

#include <stdlib.h>

static uint64_t g_next_pane_id = 1;

knox_pane_t *knox_pane_new(knox_pane_content_t content, size_t repo_idx)
{
    knox_pane_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->content = content;
    p->repo_idx = repo_idx;
    p->graph_all_branches = true;
    p->graph_show_remotes = true;
    p->id = g_next_pane_id++;
    return p;
}

void knox_pane_free(knox_pane_t *p)
{
    if (!p)
        return;
    knox_viewer_free(&p->doc);
    free(p);
}

knox_split_node_t *knox_split_create_leaf(knox_pane_t *p)
{
    knox_split_node_t *n = calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    n->type = KNOX_NODE_LEAF;
    n->pane = p;
    n->ratio = 0.5f;
    return n;
}

knox_split_node_t *knox_split_pane(knox_split_node_t *target,
                                   knox_node_type_t split_type,
                                   knox_pane_t *new_pane)
{
    if (!target || target->type != KNOX_NODE_LEAF || !new_pane)
        return NULL;
    if (split_type != KNOX_NODE_SPLIT_V && split_type != KNOX_NODE_SPLIT_H)
        return NULL;

    knox_split_node_t *c1 = knox_split_create_leaf(target->pane);
    knox_split_node_t *c2 = knox_split_create_leaf(new_pane);
    if (!c1 || !c2) {
        free(c1);
        free(c2);
        return NULL;
    }

    c1->parent = target;
    c2->parent = target;
    target->type = split_type;
    target->pane = NULL;
    target->child1 = c1;
    target->child2 = c2;
    target->ratio = 0.5f;
    return c2;
}

void knox_split_layout(knox_split_node_t *node, int x, int y, int w, int h)
{
    if (!node)
        return;
    node->x = x;
    node->y = y;
    node->w = w;
    node->h = h;

    if (node->type == KNOX_NODE_LEAF) {
        if (node->pane) {
            node->pane->x = x;
            node->pane->y = y;
            node->pane->w = w;
            node->pane->h = h;
        }
        return;
    }

    if (node->type == KNOX_NODE_SPLIT_V) {
        int w1 = (int)(w * node->ratio);
        if (w1 < 10 && w >= 21)
            w1 = 10;
        int w2 = w - w1 - 1;
        if (w2 < 1) {
            w1 = w / 2;
            w2 = w - w1;
        }
        knox_split_layout(node->child1, x, y, w1, h);
        knox_split_layout(node->child2, x + (w - w2), y, w2, h);
    } else if (node->type == KNOX_NODE_SPLIT_H) {
        int h1 = (int)(h * node->ratio);
        if (h1 < 4 && h >= 9)
            h1 = 4;
        int h2 = h - h1 - 1;
        if (h2 < 1) {
            h1 = h / 2;
            h2 = h - h1;
        }
        knox_split_layout(node->child1, x, y, w, h1);
        knox_split_layout(node->child2, x, y + (h - h2), w, h2);
    }
}

static void collect_leaves(knox_split_node_t *node, knox_pane_t **out,
                           size_t *count, size_t cap)
{
    if (!node || *count >= cap)
        return;
    if (node->type == KNOX_NODE_LEAF) {
        if (node->pane)
            out[(*count)++] = node->pane;
        return;
    }
    collect_leaves(node->child1, out, count, cap);
    collect_leaves(node->child2, out, count, cap);
}

size_t knox_split_count_leaves(const knox_split_node_t *node)
{
    if (!node)
        return 0;
    if (node->type == KNOX_NODE_LEAF)
        return node->pane ? 1 : 0;
    return knox_split_count_leaves(node->child1) +
           knox_split_count_leaves(node->child2);
}

knox_pane_t *knox_split_next_pane(knox_split_node_t *root, knox_pane_t *cur)
{
    if (!root)
        return NULL;
    knox_pane_t *leaves[32];
    size_t count = 0;
    collect_leaves(root, leaves, &count, 32);
    if (count == 0)
        return NULL;
    for (size_t i = 0; i < count; i++) {
        if (leaves[i] == cur)
            return leaves[(i + 1) % count];
    }
    return leaves[0];
}

knox_split_node_t *knox_split_find_pane(knox_split_node_t *node,
                                        knox_pane_t *p)
{
    if (!node || !p)
        return NULL;
    if (node->type == KNOX_NODE_LEAF)
        return (node->pane == p) ? node : NULL;
    knox_split_node_t *found = knox_split_find_pane(node->child1, p);
    return found ? found : knox_split_find_pane(node->child2, p);
}

static knox_pane_t *first_leaf_pane(knox_split_node_t *node)
{
    if (!node)
        return NULL;
    if (node->type == KNOX_NODE_LEAF)
        return node->pane;
    knox_pane_t *p = first_leaf_pane(node->child1);
    return p ? p : first_leaf_pane(node->child2);
}

knox_pane_t *knox_split_close(knox_split_node_t *root,
                              knox_split_node_t *target,
                              knox_split_node_t **new_root)
{
    if (!root || !target || !new_root)
        return NULL;
    if (root == target) {
        /* Cannot close the sole root leaf here; caller closes the tab */
        *new_root = root;
        return NULL;
    }

    knox_split_node_t *parent = target->parent;
    if (!parent) {
        *new_root = root;
        return NULL;
    }

    knox_split_node_t *sibling = (parent->child1 == target)
                                 ? parent->child2
                                 : parent->child1;
    knox_split_node_t *grand = parent->parent;
    sibling->parent = grand;
    if (!grand) {
        *new_root = sibling;
    } else {
        if (grand->child1 == parent)
            grand->child1 = sibling;
        else
            grand->child2 = sibling;
        *new_root = root;
    }

    knox_pane_free(target->pane);
    free(target);
    free(parent);
    return first_leaf_pane(sibling);
}

void knox_split_free_tree(knox_split_node_t *node)
{
    if (!node)
        return;
    if (node->type == KNOX_NODE_LEAF) {
        knox_pane_free(node->pane);
    } else {
        knox_split_free_tree(node->child1);
        knox_split_free_tree(node->child2);
    }
    free(node);
}
