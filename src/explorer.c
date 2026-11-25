#include "knox.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static knox_tree_node_t *tree_node_new(const char *name, const char *abs_path,
                                       bool is_dir, int depth,
                                       knox_tree_node_t *parent)
{
    knox_tree_node_t *n = calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    n->name = xstrdup(name);
    n->abs_path = xstrdup(abs_path);
    n->is_dir = is_dir;
    n->depth = depth;
    n->git_status = KNOX_CHANGE_NONE;
    n->parent = parent;
    return n;
}

static void tree_node_free(knox_tree_node_t *n)
{
    if (!n)
        return;
    for (size_t i = 0; i < n->child_count; i++)
        tree_node_free(n->children[i]);
    free(n->children);
    free(n->name);
    free(n->abs_path);
    free(n);
}

static int cmp_tree_nodes(const void *a, const void *b)
{
    const knox_tree_node_t *na = *(const knox_tree_node_t *const *)a;
    const knox_tree_node_t *nb = *(const knox_tree_node_t *const *)b;
    if (na->is_dir != nb->is_dir)
        return na->is_dir ? -1 : 1; /* Directories first */
    return strcmp(na->name, nb->name);
}

static int tree_node_load_children(knox_tree_node_t *dir_node, bool show_hidden)
{
    if (!dir_node || !dir_node->is_dir || dir_node->loaded)
        return 0;

    DIR *d = opendir(dir_node->abs_path);
    if (!d) {
        dir_node->loaded = true;
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
            strcmp(name, ".git") == 0)
            continue;
        if (!show_hidden && name[0] == '.' &&
            strcmp(name, ".gitignore") != 0)
            continue;

        char *child_path = tam_path_join(dir_node->abs_path, name);
        if (!child_path)
            continue;
        struct stat st;
        bool is_dir = (lstat(child_path, &st) == 0 && S_ISDIR(st.st_mode));

        if (dir_node->child_count == dir_node->child_cap) {
            size_t next = dir_node->child_cap ? dir_node->child_cap * 2 : 16;
            knox_tree_node_t **grown = realloc(dir_node->children,
                                               next * sizeof(*grown));
            if (!grown) {
                free(child_path);
                break;
            }
            dir_node->children = grown;
            dir_node->child_cap = next;
        }
        knox_tree_node_t *child = tree_node_new(name, child_path, is_dir,
                                                dir_node->depth + 1, dir_node);
        free(child_path);
        if (child)
            dir_node->children[dir_node->child_count++] = child;
    }
    closedir(d);

    if (dir_node->child_count > 1) {
        qsort(dir_node->children, dir_node->child_count,
              sizeof(knox_tree_node_t *), cmp_tree_nodes);
    }
    dir_node->loaded = true;
    return 0;
}

static void flatten_tree_rec(knox_explorer_t *ex, knox_tree_node_t *node)
{
    if (!node)
        return;
    if (ex->flat_count == ex->flat_cap) {
        size_t next = ex->flat_cap ? ex->flat_cap * 2 : 64;
        knox_tree_node_t **grown = realloc(ex->flat, next * sizeof(*grown));
        if (!grown)
            return;
        ex->flat = grown;
        ex->flat_cap = next;
    }
    ex->flat[ex->flat_count++] = node;
    if (node->is_dir && node->expanded) {
        for (size_t i = 0; i < node->child_count; i++)
            flatten_tree_rec(ex, node->children[i]);
    }
}

static void decorate_node_rec(knox_tree_node_t *node,
                              const knox_workspace_t *ws)
{
    if (!node)
        return;
    node->git_status = KNOX_CHANGE_NONE;
    node->has_dirty_desc = false;
    node->is_repo_root = false;

    if (ws) {
        for (size_t r = 0; r < ws->count; r++) {
            const knox_repo_t *repo = &ws->repos[r];
            size_t rlen = strlen(repo->root_path);
            if (strcmp(node->abs_path, repo->root_path) == 0) {
                node->is_repo_root = true;
                if (repo->dirty)
                    node->has_dirty_desc = true;
            } else if (strncmp(node->abs_path, repo->root_path, rlen) == 0 &&
                       node->abs_path[rlen] == '/') {
                const char *rel = node->abs_path + rlen + 1;
                size_t rel_len = strlen(rel);
                for (size_t c = 0; c < repo->change_count; c++) {
                    const knox_file_change_t *fc = &repo->changes[c];
                    if (!node->is_dir && strcmp(fc->path, rel) == 0) {
                        node->git_status = (fc->worktree_status != KNOX_CHANGE_NONE)
                                           ? fc->worktree_status
                                           : fc->index_status;
                        break;
                    } else if (node->is_dir &&
                               strncmp(fc->path, rel, rel_len) == 0 &&
                               fc->path[rel_len] == '/') {
                        node->has_dirty_desc = true;
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < node->child_count; i++) {
        decorate_node_rec(node->children[i], ws);
        if (node->children[i]->git_status != KNOX_CHANGE_NONE ||
            node->children[i]->has_dirty_desc) {
            node->has_dirty_desc = true;
        }
    }
}

void knox_explorer_refresh_git_status(knox_explorer_t *ex,
                                      const knox_workspace_t *ws)
{
    if (!ex || !ex->root)
        return;
    decorate_node_rec(ex->root, ws);
}

int knox_explorer_init(knox_explorer_t *ex, const char *root_path,
                       bool show_hidden)
{
    *ex = (knox_explorer_t){ .show_hidden = show_hidden };
    char resolved[4096];
    const char *abs_p = (root_path && realpath(root_path, resolved))
                        ? resolved
                        : (root_path ? root_path : ".");
    const char *slash = strrchr(abs_p, '/');
    const char *name = (slash && slash[1]) ? slash + 1 : abs_p;

    ex->root = tree_node_new(name, abs_p, true, 0, NULL);
    if (!ex->root)
        return -1;
    ex->root->expanded = true;
    tree_node_load_children(ex->root, show_hidden);
    flatten_tree_rec(ex, ex->root);
    return 0;
}

void knox_explorer_free(knox_explorer_t *ex)
{
    if (!ex)
        return;
    tree_node_free(ex->root);
    free(ex->flat);
    *ex = (knox_explorer_t){0};
}

int knox_explorer_toggle_node(knox_explorer_t *ex, size_t flat_idx,
                              const knox_workspace_t *ws)
{
    if (!ex || flat_idx >= ex->flat_count)
        return -1;
    knox_tree_node_t *node = ex->flat[flat_idx];
    if (!node->is_dir)
        return 0;

    node->expanded = !node->expanded;
    if (node->expanded && !node->loaded)
        tree_node_load_children(node, ex->show_hidden);

    ex->flat_count = 0;
    flatten_tree_rec(ex, ex->root);
    knox_explorer_refresh_git_status(ex, ws);
    return 1;
}

int knox_explorer_rebuild(knox_explorer_t *ex, const knox_workspace_t *ws)
{
    if (!ex || !ex->root)
        return -1;
    char *saved_root = xstrdup(ex->root->abs_path);
    bool show_hidden = ex->show_hidden;
    knox_explorer_free(ex);
    int rc = knox_explorer_init(ex, saved_root, show_hidden);
    free(saved_root);
    knox_explorer_refresh_git_status(ex, ws);
    return rc;
}
