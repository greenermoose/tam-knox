#include "knox.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char *const VALID_WIDGETS[] = {
    "view-source-control",
    "view-explorer",
    "explore-repo",
    "view-graph",
    "toggle-sidebar",
    "focus-next",
    "focus-prev",
    "open-or-inspect",
    "stage-or-page-down",
    "repo-fetch",
    "repo-sync",
    "fetch-all",
    "commit-prompt",
    "branch-modal",
    "stash-modal",
    "toggle-diff-split",
    "open-in-editor",
    "toggle-render-mode",
    "cycle-branch-filter",
    "split-vertical",
    "split-horizontal",
    "focus-other-pane",
    "tab-prev",
    "tab-next",
    "tab-1",
    "tab-2",
    "tab-3",
    "tab-4",
    "tab-5",
    "tab-6",
    "tab-7",
    "tab-8",
    "tab-9",
    "tab-0",
    "new",
    "close",
    "find",
    "find-next",
    "find-prev",
    "update",
    "copy",
    "execute",
    "guidance",
    "clear-screen",
    "quit",
    /* Navigation & mode widgets bindable via keymap.ini */
    "move-up",
    "move-down",
    "page-up",
    "page-down",
    "jump-top",
    "jump-bottom",
    "scroll-left",
    "scroll-right",
    "cancel-mode",
    /* Unbind directives */
    "unbind",
    "none"
};

#define VALID_WIDGET_COUNT (sizeof(VALID_WIDGETS) / sizeof(VALID_WIDGETS[0]))

void knox_keymap_init(knox_keymap_t *km)
{
    if (!km)
        return;
    *km = (knox_keymap_t){0};
}

void knox_keymap_free(knox_keymap_t *km)
{
    if (!km)
        return;
    free(km->entries);
    *km = (knox_keymap_t){0};
}

bool knox_keymap_is_valid_widget(const char *widget)
{
    if (!widget || !*widget)
        return false;
    for (size_t i = 0; i < VALID_WIDGET_COUNT; i++) {
        if (strcmp(VALID_WIDGETS[i], widget) == 0)
            return true;
    }
    return false;
}

bool knox_keymap_parse_key(const char *spec, tam_key_type_t *out_type,
                           uint32_t *out_cp, unsigned *out_mods)
{
    /* libtam's key names: the result is exactly what tam_key reports for
     * that key, so lookups compare fields directly. Meta is refused (it
     * belongs to tam-pane). */
    if (!spec || !out_type || !out_cp || !out_mods)
        return false;
    tam_key_event_t ev;
    if (!tam_key_name_parse(spec, &ev))
        return false;
    *out_type = ev.type;
    *out_cp = ev.codepoint;
    *out_mods = ev.mods;
    return true;
}

void knox_keymap_format_key(tam_key_type_t type, uint32_t cp,
                            unsigned mods, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    tam_key_event_t ev = {.type = type, .codepoint = cp, .mods = mods};
    (void)tam_key_name_format(&ev, out, cap);
}

static bool keymap_upsert(knox_keymap_t *km, tam_key_type_t type,
                          uint32_t cp, unsigned mods, const char *widget)
{
    for (size_t i = 0; i < km->count; i++) {
        if (km->entries[i].type == type &&
            km->entries[i].codepoint == cp &&
            km->entries[i].mods == mods) {
            snprintf(km->entries[i].widget, sizeof(km->entries[i].widget),
                     "%s", widget);
            return true;
        }
    }

    if (km->count == km->cap) {
        size_t next = km->cap ? km->cap * 2 : 16;
        knox_keybind_t *grown = realloc(km->entries, next * sizeof(*grown));
        if (!grown)
            return false;
        km->entries = grown;
        km->cap = next;
    }

    knox_keybind_t *e = &km->entries[km->count++];
    *e = (knox_keybind_t){
        .type = type,
        .codepoint = cp,
        .mods = mods
    };
    knox_keymap_format_key(type, cp, mods, e->key_label, sizeof(e->key_label));
    snprintf(e->widget, sizeof(e->widget), "%s", widget);
    return true;
}

static int keymap_ini_handler(void *user, const char *section,
                              const char *name, const char *value)
{
    knox_keymap_t *km = (knox_keymap_t *)user;
    if (!km || !name || !value)
        return 0;

    if (section && *section &&
        strcasecmp(section, "keymap") != 0 &&
        strcasecmp(section, "bindings") != 0 &&
        strcasecmp(section, "keys") != 0) {
        return 0; /* Ignore unrelated sections cleanly */
    }

    tam_key_type_t type = TAM_KEY_NONE;
    uint32_t cp = 0;
    unsigned mods = 0;
    if (!knox_keymap_parse_key(name, &type, &cp, &mods)) {
        km->warning_count++;
        snprintf(km->last_warning, sizeof(km->last_warning),
                 "keymap.ini: invalid key '%s'", name);
        return 0;
    }

    if (!knox_keymap_is_valid_widget(value)) {
        km->warning_count++;
        snprintf(km->last_warning, sizeof(km->last_warning),
                 "keymap.ini: unknown widget '%s' for key '%s'", value, name);
        return 0;
    }

    keymap_upsert(km, type, cp, mods, value);
    return 0;
}

int knox_keymap_load_string(knox_keymap_t *km, const char *ini_text)
{
    if (!km || !ini_text)
        return -1;
    int rc = tam_ini_parse_string(ini_text, keymap_ini_handler, km);
    return (rc == 0) ? 0 : -1;
}

int knox_keymap_load_file(knox_keymap_t *km, const char *path)
{
    if (!km || !path || !*path)
        return -1;
    if (!tam_fs_file_exists(path))
        return -1;
    int rc = tam_ini_parse_file(path, keymap_ini_handler, km);
    if (rc == 0) {
        snprintf(km->source_path, sizeof(km->source_path), "%s", path);
        return 0;
    }
    return -1;
}

int knox_keymap_load_default(knox_keymap_t *km)
{
    if (!km)
        return -1;

    /* 1. Explicit env override (useful for testing and custom profiles) */
    const char *env_path = getenv("TAM_KNOX_KEYMAP");
    if (env_path && *env_path)
        return knox_keymap_load_file(km, env_path);

    /* 2. Standard Tamlinux XDG path: ~/.config/tamlinux/tam-knox/keymap.ini */
    char *xdg_path = tam_xdg_path(TAM_XDG_CONFIG, "tam-knox", "keymap.ini", false);
    if (xdg_path) {
        if (tam_fs_file_exists(xdg_path)) {
            int rc = knox_keymap_load_file(km, xdg_path);
            free(xdg_path);
            return rc;
        }
        free(xdg_path);
    }

    /* 3. Fallback XDG path: ~/.config/tam-knox/keymap.ini */
    const char *xdg_cfg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char fallback[512] = "";
    if (xdg_cfg && xdg_cfg[0] == '/')
        snprintf(fallback, sizeof(fallback), "%s/tam-knox/keymap.ini", xdg_cfg);
    else if (home && home[0] == '/')
        snprintf(fallback, sizeof(fallback), "%s/.config/tam-knox/keymap.ini", home);

    if (fallback[0] && tam_fs_file_exists(fallback))
        return knox_keymap_load_file(km, fallback);

    return 0; /* Missing keymap.ini is normal: defaults apply */
}

const char *knox_keymap_lookup(const knox_keymap_t *km,
                               const tam_key_event_t *ev)
{
    if (!km || !ev || km->count == 0)
        return NULL;

    /* Parsed names match decoded events field for field. */
    uint32_t cp = ev->codepoint;
    unsigned mods = ev->mods;

    for (size_t i = 0; i < km->count; i++) {
        const knox_keybind_t *e = &km->entries[i];
        if (e->type == ev->type && e->mods == mods) {
            if (ev->type != TAM_KEY_CHAR || e->codepoint == cp)
                return e->widget;
        }
    }
    return NULL;
}

bool knox_keymap_find_widget_key(const knox_keymap_t *km,
                                 const char *widget,
                                 char *out, size_t cap)
{
    if (!km || !widget || !out || cap == 0)
        return false;
    for (size_t i = 0; i < km->count; i++) {
        if (strcmp(km->entries[i].widget, widget) == 0) {
            snprintf(out, cap, "%s", km->entries[i].key_label);
            return true;
        }
    }
    return false;
}
