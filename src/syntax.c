#include "knox.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

static bool has_suffix(const char *str, const char *ext)
{
    size_t slen = strlen(str);
    size_t elen = strlen(ext);
    return slen >= elen && strcasecmp(str + slen - elen, ext) == 0;
}

knox_lang_t knox_syntax_detect(const char *path, const char *first_line,
                               size_t first_len)
{
    if (path && *path) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;

        if (strcmp(base, "Makefile") == 0 || strcmp(base, "GNUmakefile") == 0 ||
            has_suffix(base, ".mk"))
            return KNOX_LANG_MAKEFILE;
        if (has_suffix(base, ".c") || has_suffix(base, ".h") ||
            has_suffix(base, ".cpp") || has_suffix(base, ".hpp") ||
            has_suffix(base, ".cc"))
            return KNOX_LANG_C;
        if (has_suffix(base, ".sh") || has_suffix(base, ".bash") ||
            has_suffix(base, ".zsh") || strcmp(base, "PKGBUILD") == 0)
            return KNOX_LANG_SHELL;
        if (has_suffix(base, ".py"))
            return KNOX_LANG_PYTHON;
        if (has_suffix(base, ".js") || has_suffix(base, ".ts") ||
            has_suffix(base, ".qml") || has_suffix(base, ".mjs"))
            return KNOX_LANG_JS;
        if (has_suffix(base, ".json") || has_suffix(base, ".jsonc"))
            return KNOX_LANG_JSON;
        if (has_suffix(base, ".yaml") || has_suffix(base, ".yml"))
            return KNOX_LANG_YAML;
        if (has_suffix(base, ".toml") || has_suffix(base, ".ini") ||
            has_suffix(base, ".conf") || has_suffix(base, ".cfg"))
            return KNOX_LANG_TOML_INI;
        if (has_suffix(base, ".md") || has_suffix(base, ".markdown"))
            return KNOX_LANG_MARKDOWN;
        if (has_suffix(base, ".html") || has_suffix(base, ".htm") ||
            has_suffix(base, ".xml") || has_suffix(base, ".svg"))
            return KNOX_LANG_HTML;
        if (has_suffix(base, ".rs"))
            return KNOX_LANG_RUST;
        if (has_suffix(base, ".go"))
            return KNOX_LANG_GO;
        if (has_suffix(base, ".diff") || has_suffix(base, ".patch"))
            return KNOX_LANG_DIFF;
    }

    if (first_line && first_len >= 2 &&
        first_line[0] == '#' && first_line[1] == '!') {
        if (memmem(first_line, first_len, "python", 6))
            return KNOX_LANG_PYTHON;
        if (memmem(first_line, first_len, "sh", 2) ||
            memmem(first_line, first_len, "bash", 4) ||
            memmem(first_line, first_len, "tam-clam", 8))
            return KNOX_LANG_SHELL;
        if (memmem(first_line, first_len, "node", 4))
            return KNOX_LANG_JS;
    }
    return KNOX_LANG_PLAIN;
}

const char *knox_syntax_lang_name(knox_lang_t lang)
{
    switch (lang) {
    case KNOX_LANG_C:        return "C";
    case KNOX_LANG_SHELL:    return "Shell";
    case KNOX_LANG_PYTHON:   return "Python";
    case KNOX_LANG_JS:       return "JS/QML";
    case KNOX_LANG_JSON:     return "JSON";
    case KNOX_LANG_YAML:     return "YAML";
    case KNOX_LANG_TOML_INI: return "INI/TOML";
    case KNOX_LANG_MAKEFILE: return "Makefile";
    case KNOX_LANG_MARKDOWN: return "Markdown";
    case KNOX_LANG_HTML:     return "HTML";
    case KNOX_LANG_RUST:     return "Rust";
    case KNOX_LANG_GO:       return "Go";
    case KNOX_LANG_DIFF:     return "Diff";
    default:                 return "Text";
    }
}

const char *knox_syntax_token_sgr(knox_token_kind_t kind, bool linux_vt)
{
    switch (kind) {
    case KNOX_TOK_KEYWORD:   return "\033[1;35m"; /* Bold Magenta */
    case KNOX_TOK_TYPE:      return "\033[1;36m"; /* Bold Cyan */
    case KNOX_TOK_STRING:    return "\033[32m";   /* Green */
    case KNOX_TOK_NUMBER:    return "\033[1;33m"; /* Bold Yellow */
    case KNOX_TOK_COMMENT:   return linux_vt ? "\033[36m" : "\033[38;5;247m"; /* High-contrast muted gray */
    case KNOX_TOK_PREPROC:   return "\033[1;34m"; /* Bold Blue */
    case KNOX_TOK_HEADING:   return "\033[1;36m"; /* Bold Cyan */
    case KNOX_TOK_KEY:       return "\033[1;36m"; /* Bold Cyan */
    case KNOX_TOK_DIFF_ADD:  return "\033[1;32m"; /* Bold Green */
    case KNOX_TOK_DIFF_DEL:  return "\033[1;31m"; /* Bold Red */
    case KNOX_TOK_DIFF_HUNK: return "\033[1;36m"; /* Bold Cyan */
    case KNOX_TOK_DIFF_HDR:  return "\033[1;33m"; /* Bold Yellow */
    default:                 return "";
    }
}

static const char *const C_KEYWORDS[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "return", "goto", "sizeof", "typedef",
    "struct", "union", "enum", "static", "extern", "inline", "const",
    "volatile", "restrict", "_Alignas", "_Alignof", "_Atomic",
    "_Generic", "_Noreturn", "_Static_assert", "_Thread_local", NULL
};

static const char *const C_TYPES[] = {
    "void", "char", "short", "int", "long", "float", "double", "signed",
    "unsigned", "bool", "size_t", "ssize_t", "off_t", "ptrdiff_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "uintptr_t", "intptr_t", "FILE", "DIR",
    "true", "false", "NULL", NULL
};

static const char *const SH_KEYWORDS[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "while",
    "until", "do", "done", "in", "function", "select", "return", "exit",
    "break", "continue", "local", "export", "readonly", "set", "unset",
    "shift", "trap", "eval", "exec", NULL
};

static const char *const PY_KEYWORDS[] = {
    "def", "class", "if", "elif", "else", "for", "while", "try", "except",
    "finally", "with", "as", "import", "from", "return", "yield", "pass",
    "break", "continue", "raise", "in", "is", "not", "and", "or",
    "lambda", "global", "nonlocal", "assert", "del", "async", "await",
    "True", "False", "None", NULL
};

static const char *const JS_KEYWORDS[] = {
    "function", "const", "let", "var", "if", "else", "for", "while", "do",
    "switch", "case", "default", "break", "continue", "return", "class",
    "extends", "new", "this", "super", "import", "export", "from", "try",
    "catch", "finally", "throw", "async", "await", "yield", "typeof",
    "instanceof", "in", "of", "property", "signal", "readonly",
    "true", "false", "null", "undefined", NULL
};

static const char *const RUST_KEYWORDS[] = {
    "fn", "let", "mut", "const", "static", "pub", "mod", "use", "crate",
    "struct", "enum", "union", "trait", "impl", "type", "where", "if",
    "else", "match", "for", "while", "loop", "break", "continue",
    "return", "async", "await", "move", "ref", "unsafe", "extern",
    "self", "Self", "super", "true", "false", NULL
};

static const char *const GO_KEYWORDS[] = {
    "func", "var", "const", "type", "struct", "interface", "package",
    "import", "if", "else", "for", "range", "switch", "case", "default",
    "select", "go", "defer", "chan", "map", "return", "break",
    "continue", "fallthrough", "goto", "true", "false", "nil", NULL
};

static bool word_in_list(const char *w, size_t len, const char *const *list)
{
    if (!list || len == 0)
        return false;
    for (size_t i = 0; list[i]; i++) {
        if (strlen(list[i]) == len && strncmp(w, list[i], len) == 0)
            return true;
    }
    return false;
}

static bool is_ident_start(unsigned char c)
{
    return isalpha(c) || c == '_';
}

static bool is_ident_char(unsigned char c)
{
    return isalnum(c) || c == '_';
}

knox_lex_state_t knox_syntax_highlight_line(knox_lang_t lang,
                                            knox_lex_state_t in_state,
                                            const char *line, size_t len,
                                            uint8_t *out_tokens)
{
    if (!out_tokens || len == 0)
        return in_state;
    memset(out_tokens, KNOX_TOK_NORMAL, len);

    if (lang == KNOX_LANG_PLAIN)
        return KNOX_LEX_NORMAL;

    /* 1. Unified Diff */
    if (lang == KNOX_LANG_DIFF) {
        if (len >= 3 && (strncmp(line, "---", 3) == 0 ||
                         strncmp(line, "+++", 3) == 0 ||
                         strncmp(line, "diff ", 5) == 0 ||
                         strncmp(line, "commit ", 7) == 0 ||
                         strncmp(line, "index ", 6) == 0)) {
            memset(out_tokens, KNOX_TOK_DIFF_HDR, len);
        } else if (len >= 2 && strncmp(line, "@@", 2) == 0) {
            memset(out_tokens, KNOX_TOK_DIFF_HUNK, len);
        } else if (line[0] == '+') {
            memset(out_tokens, KNOX_TOK_DIFF_ADD, len);
        } else if (line[0] == '-') {
            memset(out_tokens, KNOX_TOK_DIFF_DEL, len);
        }
        return KNOX_LEX_NORMAL;
    }

    /* 2. Markdown (Raw View) */
    if (lang == KNOX_LANG_MARKDOWN) {
        size_t i = 0;
        while (i < len && (line[i] == ' ' || line[i] == '\t'))
            i++;
        if (i < len && line[i] == '#') {
            memset(out_tokens, KNOX_TOK_HEADING, len);
            return in_state;
        }
        if (i + 2 < len && strncmp(line + i, "```", 3) == 0) {
            memset(out_tokens, KNOX_TOK_PREPROC, len);
            return in_state;
        }
        for (size_t j = 0; j < len; j++) {
            if (line[j] == '`') {
                size_t k = j + 1;
                while (k < len && line[k] != '`')
                    k++;
                if (k < len)
                    k++;
                memset(out_tokens + j, KNOX_TOK_STRING, k - j);
                j = k - 1;
            }
        }
        return in_state;
    }

    /* 3. HTML / XML (Raw View) */
    if (lang == KNOX_LANG_HTML) {
        size_t i = 0;
        while (i < len) {
            if (in_state == KNOX_LEX_BLOCK_COMMENT) {
                const char *end = memmem(line + i, len - i, "-->", 3);
                if (!end) {
                    memset(out_tokens + i, KNOX_TOK_COMMENT, len - i);
                    return KNOX_LEX_BLOCK_COMMENT;
                }
                size_t elen = (size_t)(end - (line + i)) + 3;
                memset(out_tokens + i, KNOX_TOK_COMMENT, elen);
                i += elen;
                in_state = KNOX_LEX_NORMAL;
                continue;
            }
            if (i + 3 < len && strncmp(line + i, "<!--", 4) == 0) {
                in_state = KNOX_LEX_BLOCK_COMMENT;
                continue;
            }
            if (line[i] == '<') {
                size_t k = i + 1;
                while (k < len && line[k] != '>') {
                    if (line[k] == '"' || line[k] == '\'') {
                        char q = line[k++];
                        size_t qs = k - 1;
                        while (k < len && line[k] != q)
                            k++;
                        if (k < len)
                            k++;
                        memset(out_tokens + qs, KNOX_TOK_STRING, k - qs);
                    } else {
                        out_tokens[k] = KNOX_TOK_KEYWORD;
                        k++;
                    }
                }
                out_tokens[i] = KNOX_TOK_KEYWORD;
                if (k < len) {
                    out_tokens[k] = KNOX_TOK_KEYWORD;
                    k++;
                }
                i = k;
                continue;
            }
            i++;
        }
        return in_state;
    }

    /* 4. YAML / TOML / INI / Makefile key-value & comment rules */
    if (lang == KNOX_LANG_YAML || lang == KNOX_LANG_TOML_INI ||
        lang == KNOX_LANG_MAKEFILE) {
        size_t i = 0;
        while (i < len && (line[i] == ' ' || line[i] == '\t'))
            i++;
        if (i < len && (line[i] == '#' ||
                        (lang == KNOX_LANG_TOML_INI && line[i] == ';'))) {
            memset(out_tokens + i, KNOX_TOK_COMMENT, len - i);
            return KNOX_LEX_NORMAL;
        }
        if (lang == KNOX_LANG_TOML_INI && i < len && line[i] == '[') {
            memset(out_tokens + i, KNOX_TOK_HEADING, len - i);
            return KNOX_LEX_NORMAL;
        }
        /* Highlight key before ':' or '=' */
        const char *sep = memchr(line + i,
                                 (lang == KNOX_LANG_YAML) ? ':' : '=',
                                 len - i);
        if (!sep && lang == KNOX_LANG_MAKEFILE)
            sep = memchr(line + i, ':', len - i);
        if (sep) {
            size_t klen = (size_t)(sep - (line + i));
            memset(out_tokens + i, KNOX_TOK_KEY, klen);
        }
    }

    /* 5. Code languages (C, Shell, Python, JS/QML, JSON, Rust, Go, etc.) */
    const char *const *kw_list = NULL;
    const char *const *ty_list = NULL;
    bool has_c_comments = false;
    bool has_hash_comments = false;

    switch (lang) {
    case KNOX_LANG_C:
        kw_list = C_KEYWORDS;
        ty_list = C_TYPES;
        has_c_comments = true;
        break;
    case KNOX_LANG_SHELL:
        kw_list = SH_KEYWORDS;
        has_hash_comments = true;
        break;
    case KNOX_LANG_PYTHON:
        kw_list = PY_KEYWORDS;
        has_hash_comments = true;
        break;
    case KNOX_LANG_JS:
        kw_list = JS_KEYWORDS;
        has_c_comments = true;
        break;
    case KNOX_LANG_RUST:
        kw_list = RUST_KEYWORDS;
        ty_list = C_TYPES;
        has_c_comments = true;
        break;
    case KNOX_LANG_GO:
        kw_list = GO_KEYWORDS;
        ty_list = C_TYPES;
        has_c_comments = true;
        break;
    case KNOX_LANG_JSON:
        has_c_comments = true; /* JSONC */
        break;
    default:
        has_hash_comments = true;
        break;
    }

    size_t i = 0;
    if (lang == KNOX_LANG_C && in_state == KNOX_LEX_NORMAL) {
        size_t s = 0;
        while (s < len && (line[s] == ' ' || line[s] == '\t'))
            s++;
        if (s < len && line[s] == '#') {
            memset(out_tokens + s, KNOX_TOK_PREPROC, len - s);
            return KNOX_LEX_NORMAL;
        }
    }

    while (i < len) {
        if (in_state == KNOX_LEX_BLOCK_COMMENT) {
            const char *end = memmem(line + i, len - i, "*/", 2);
            if (!end) {
                memset(out_tokens + i, KNOX_TOK_COMMENT, len - i);
                return KNOX_LEX_BLOCK_COMMENT;
            }
            size_t clen = (size_t)(end - (line + i)) + 2;
            memset(out_tokens + i, KNOX_TOK_COMMENT, clen);
            i += clen;
            in_state = KNOX_LEX_NORMAL;
            continue;
        }

        if (has_c_comments && i + 1 < len &&
            line[i] == '/' && line[i + 1] == '/') {
            memset(out_tokens + i, KNOX_TOK_COMMENT, len - i);
            break;
        }
        if (has_c_comments && i + 1 < len &&
            line[i] == '/' && line[i + 1] == '*') {
            out_tokens[i] = KNOX_TOK_COMMENT;
            out_tokens[i + 1] = KNOX_TOK_COMMENT;
            i += 2;
            in_state = KNOX_LEX_BLOCK_COMMENT;
            continue;
        }
        if (has_hash_comments && line[i] == '#') {
            memset(out_tokens + i, KNOX_TOK_COMMENT, len - i);
            break;
        }

        /* Strings */
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i];
            size_t start = i++;
            while (i < len) {
                if (line[i] == '\\' && i + 1 < len) {
                    i += 2;
                    continue;
                }
                if (line[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            knox_token_kind_t skind = KNOX_TOK_STRING;
            if (lang == KNOX_LANG_JSON && q == '"') {
                size_t p = i;
                while (p < len && (line[p] == ' ' || line[p] == '\t'))
                    p++;
                if (p < len && line[p] == ':')
                    skind = KNOX_TOK_KEY;
            }
            memset(out_tokens + start, skind, i - start);
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)line[i]) &&
            (i == 0 || !is_ident_char((unsigned char)line[i - 1]))) {
            size_t start = i++;
            while (i < len && (isalnum((unsigned char)line[i]) ||
                               line[i] == '.' || line[i] == '_'))
                i++;
            memset(out_tokens + start, KNOX_TOK_NUMBER, i - start);
            continue;
        }

        /* Identifiers / Keywords / Types */
        if (is_ident_start((unsigned char)line[i])) {
            size_t start = i++;
            while (i < len && is_ident_char((unsigned char)line[i]))
                i++;
            size_t wlen = i - start;
            if (word_in_list(line + start, wlen, kw_list)) {
                memset(out_tokens + start, KNOX_TOK_KEYWORD, wlen);
            } else if (word_in_list(line + start, wlen, ty_list) ||
                       (wlen > 2 && strncmp(line + start + wlen - 2, "_t", 2) == 0)) {
                memset(out_tokens + start, KNOX_TOK_TYPE, wlen);
            }
            continue;
        }

        i++;
    }

    return in_state;
}
