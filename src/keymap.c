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
    "view-graph",
    "toggle-sidebar",
    "focus-next",
    "focus-prev",
    "open-or-inspect",
    "stage-or-page-down",
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

static char *trim_local(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    return s;
}

bool knox_keymap_parse_key(const char *spec, tam_key_type_t *out_type,
                           uint32_t *out_cp, unsigned *out_mods)
{
    if (!spec || !*spec || !out_type || !out_cp || !out_mods)
        return false;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s", spec);
    const char *p = trim_local(buf);
    if (!*p)
        return false;

    unsigned mods = 0;
    while (*p) {
        if (strncasecmp(p, "meta", 4) == 0 && (p[4] == '+' || p[4] == '-')) {
            /* Meta belongs exclusively to tam-pane (Tamlinux keybinding standard). */
            return false;
        } else if (strncasecmp(p, "ctrl", 4) == 0 && (p[4] == '+' || p[4] == '-')) {
            mods |= TAM_MOD_CTRL;
            p += 5;
        } else if (strncasecmp(p, "alt", 3) == 0 && (p[3] == '+' || p[3] == '-')) {
            mods |= TAM_MOD_ALT;
            p += 4;
        } else if (strncasecmp(p, "shift", 5) == 0 && (p[5] == '+' || p[5] == '-')) {
            mods |= TAM_MOD_SHIFT;
            p += 6;
        } else {
            break;
        }
    }

    if (!*p)
        return false;

    tam_key_type_t type = TAM_KEY_NONE;
    uint32_t cp = 0;

    if (strcasecmp(p, "enter") == 0 || strcasecmp(p, "return") == 0) {
        type = TAM_KEY_ENTER;
    } else if (strcasecmp(p, "tab") == 0) {
        type = TAM_KEY_TAB;
    } else if (strcasecmp(p, "backtab") == 0) {
        type = TAM_KEY_TAB;
        mods |= TAM_MOD_SHIFT;
    } else if (strcasecmp(p, "backspace") == 0) {
        type = TAM_KEY_BACKSPACE;
    } else if (strcasecmp(p, "escape") == 0 || strcasecmp(p, "esc") == 0) {
        type = TAM_KEY_ESC;
    } else if (strcasecmp(p, "space") == 0) {
        type = TAM_KEY_CHAR;
        cp = ' ';
    } else if (strcasecmp(p, "up") == 0) {
        type = TAM_KEY_UP;
    } else if (strcasecmp(p, "down") == 0) {
        type = TAM_KEY_DOWN;
    } else if (strcasecmp(p, "left") == 0) {
        type = TAM_KEY_LEFT;
    } else if (strcasecmp(p, "right") == 0) {
        type = TAM_KEY_RIGHT;
    } else if (strcasecmp(p, "home") == 0) {
        type = TAM_KEY_HOME;
    } else if (strcasecmp(p, "end") == 0) {
        type = TAM_KEY_END;
    } else if (strcasecmp(p, "pageup") == 0 || strcasecmp(p, "pgup") == 0) {
        type = TAM_KEY_PAGE_UP;
    } else if (strcasecmp(p, "pagedown") == 0 || strcasecmp(p, "pgdn") == 0) {
        type = TAM_KEY_PAGE_DOWN;
    } else if (strcasecmp(p, "insert") == 0 || strcasecmp(p, "ins") == 0) {
        type = TAM_KEY_INSERT;
    } else if (strcasecmp(p, "delete") == 0 || strcasecmp(p, "del") == 0) {
        type = TAM_KEY_DELETE;
    } else if ((p[0] == 'f' || p[0] == 'F') && isdigit((unsigned char)p[1])) {
        char *end = NULL;
        long fnum = strtol(p + 1, &end, 10);
        if (!end || *end != '\0' || fnum < 1 || fnum > 12)
            return false;
        type = (tam_key_type_t)(TAM_KEY_F1 + (fnum - 1));
    } else if (strlen(p) == 1 && (unsigned char)p[0] >= 0x20 &&
               (unsigned char)p[0] < 0x7f) {
        type = TAM_KEY_CHAR;
        unsigned char ch = (unsigned char)p[0];
        if (mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) {
            cp = (uint32_t)tolower(ch);
            if ((mods & TAM_MOD_CTRL) && cp == '\\')
                cp = '|'; /* Matches tam_key's 0x1c -> 'a' + 27 decoding */
        } else if (mods & TAM_MOD_SHIFT) {
            const char *plain = "`1234567890-=[]\\;',./";
            const char *shifted = "~!@#$%^&*()_+{}|:\"<>?";
            const char *found = strchr(plain, (int)tolower(ch));
            cp = found ? (uint32_t)(unsigned char)shifted[found - plain]
                       : (uint32_t)toupper(ch);
            mods &= ~TAM_MOD_SHIFT;
        } else {
            cp = (uint32_t)ch;
        }
    } else {
        return false;
    }

    *out_type = type;
    *out_cp = cp;
    *out_mods = mods;
    return true;
}

void knox_keymap_format_key(tam_key_type_t type, uint32_t cp,
                            unsigned mods, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;

    char prefix[32] = "";
    if (mods & TAM_MOD_CTRL)
        strncat(prefix, "Ctrl+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & TAM_MOD_ALT)
        strncat(prefix, "Alt+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & TAM_MOD_SHIFT)
        strncat(prefix, "Shift+", sizeof(prefix) - strlen(prefix) - 1);

    const char *kname = NULL;
    char ch_buf[16] = "";
    switch (type) {
    case TAM_KEY_ENTER:     kname = "Enter"; break;
    case TAM_KEY_TAB:       kname = "Tab"; break;
    case TAM_KEY_BACKSPACE: kname = "Backspace"; break;
    case TAM_KEY_ESC:       kname = "Escape"; break;
    case TAM_KEY_UP:        kname = "Up"; break;
    case TAM_KEY_DOWN:      kname = "Down"; break;
    case TAM_KEY_LEFT:      kname = "Left"; break;
    case TAM_KEY_RIGHT:     kname = "Right"; break;
    case TAM_KEY_HOME:      kname = "Home"; break;
    case TAM_KEY_END:       kname = "End"; break;
    case TAM_KEY_PAGE_UP:   kname = "PageUp"; break;
    case TAM_KEY_PAGE_DOWN: kname = "PageDown"; break;
    case TAM_KEY_INSERT:    kname = "Insert"; break;
    case TAM_KEY_DELETE:    kname = "Delete"; break;
    case TAM_KEY_CHAR:
        if (cp == ' ') {
            kname = "Space";
        } else if ((mods & TAM_MOD_CTRL) && cp == '|') {
            kname = "\\";
        } else if ((mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
                   cp >= 'a' && cp <= 'z') {
            snprintf(ch_buf, sizeof(ch_buf), "%c", (char)toupper((int)cp));
            kname = ch_buf;
        } else if (cp >= 0x21 && cp < 0x7f) {
            snprintf(ch_buf, sizeof(ch_buf), "%c", (char)cp);
            kname = ch_buf;
        } else {
            snprintf(ch_buf, sizeof(ch_buf), "U+%X", cp);
            kname = ch_buf;
        }
        break;
    default:
        if (type >= TAM_KEY_F1 && type <= TAM_KEY_F12) {
            snprintf(ch_buf, sizeof(ch_buf), "F%d",
                     (int)(type - TAM_KEY_F1 + 1));
            kname = ch_buf;
        } else {
            kname = "?";
        }
        break;
    }
    snprintf(out, cap, "%s%s", prefix, kname);
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

    uint32_t cp = ev->codepoint;
    unsigned mods = ev->mods;
    if (ev->type == TAM_KEY_CHAR && (mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) &&
        cp >= 'A' && cp <= 'Z') {
        cp = (uint32_t)tolower((int)cp);
    }

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
