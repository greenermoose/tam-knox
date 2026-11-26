# tam-knox Architecture Overview

`tam-knox` is a pure C17 terminal application linking only `libc` and
`libtam.a` (`tam.h`, `tam_tty.h`, `tam_key.h`, `tam_pager.h`,
`tam_uni.h`).

## Module Layout

| Module | Files | Responsibility |
| --- | --- | --- |
| **Core & CLI** | `include/knox.h`, `src/main.c` | Argument parsing (`--status`, `--graph`, `--ascii`, `--no-color`, `--version`, `--help`), terminal lifecycle, top-level entry |
| **Git Engine** | `src/git.c` | Multi-repo discovery, `git status --porcelain=v2 --branch -z` parser, path-scoped stage/unstage/commit, remote sync (`fetch`, `pull --ff-only`, `push`), non-destructive multi-repo batch fetch (`fetch-all`), branch listing/checkout/creation, stash management (`list`, `push`, `apply`, `pop`, `drop`, `show`), commit log & ref loader, commit details & numstat diff loader |
| **Git Graph** | `src/graph.c` | C17 implementation of `mhutchie.git-graph`'s `Vertex`, `Branch`, and `determinePath` layout algorithm + Unicode/ASCII cell matrix rasterizer |
| **Explorer** | `src/explorer.c` | Lazy hierarchical directory tree with directory-first sorting, live case-insensitive substring filtering (bounded auto-descent preserving expand state), and live Git status decorations (`M`/`A`/`D`/`R`/`U` on files, dirty indicator dot on ancestor folders) |
| **User Keymap** | `src/keymap.c` | `libtam` `tam_ini`-based parser and dispatcher for optional `~/.config/tamlinux/tam-knox/keymap.ini` overrides, physical key validation (rejecting `Meta+`), and `unbind` support |
| **Syntax Lexer** | `src/syntax.c` | Zero-dependency C17 line tokenizer and ANSI colorizer for C/C++, Shell, Python, JS/TS/QML, JSON, YAML, TOML, INI, Makefile, Markdown, HTML, Rust, Go, and Unified Diffs |
| **File & Diff Viewer** | `src/viewer.c`, with libtam-render (`tam_render.h`) | Dual-mode file viewer (Raw syntax-colored with line numbers & breadcrumb header vs. Rendered Markdown/HTML/Mermaid from libtam-render, shared with `tam-view`) and Unified / Side-by-Side Split Diff viewer |
| **Split Panes & Tabs** | `src/pane.c` | Binary split-pane tree (`KNOX_SPLIT_V`, `KNOX_SPLIT_H`) inside each workbench tab, geometry layout, focus cycling, and hit-testing |
| **Workbench TUI** | `src/workbench.c` | Screen compositor, Sidebar (Source Control Repos + Changes / Explorer), Tab Bar, Branch & Stash Modals (`Alt+K`, `Alt+Z`), Command Palette & Chord Composer (`Ctrl+E`), Guidance Overlay (`g` / `Ctrl+G`), and `tam_key` event loop |

## Subprocess Safety

All Git invocations use `libtam`'s `tam_exec` / `tam_exec_opts` with
explicit `argv[]` arrays (never invoking `/bin/sh`), a bounded monotonic
timeout (15s for local queries, 60s for network fetch/pull/push), and
NUL-delimited output (`-z`) wherever paths are parsed so filenames with
spaces or unusual bytes cannot corrupt parsing.
