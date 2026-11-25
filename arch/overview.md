# tam-knox Architecture Overview

`tam-knox` is a pure C11 terminal application linking only `libc` and
`libtam.a` (`tam.h`, `tam_tty.h`, `tam_key.h`, `tam_pager.h`,
`tam_uni.h`).

## Module Layout

| Module | Files | Responsibility |
| --- | --- | --- |
| **Core & CLI** | `include/knox.h`, `src/main.c` | Argument parsing (`--status`, `--graph`, `--ascii`, `--no-color`, `--version`, `--help`), terminal lifecycle, top-level entry |
| **Git Engine** | `src/git.c` | Multi-repo discovery, `git status --porcelain=v2 --branch -z` parser, path-scoped stage/unstage/commit, remote sync (`fetch`, `pull --ff-only`, `push`), commit log & ref loader, commit details & numstat diff loader |
| **Git Graph** | `src/graph.c` | C11 implementation of `mhutchie.git-graph`'s `Vertex`, `Branch`, and `determinePath` layout algorithm + Unicode/ASCII cell matrix rasterizer |
| **Explorer** | `src/explorer.c` | Lazy hierarchical directory tree with directory-first sorting and live Git status decorations (`M`/`A`/`D`/`R`/`U` on files, dirty indicator dot on ancestor folders) |
| **Syntax Lexer** | `src/syntax.c` | Zero-dependency C11 line tokenizer and ANSI colorizer for C/C++, Shell, Python, JS/TS/QML, JSON, YAML, TOML, INI, Makefile, Markdown, HTML, Rust, Go, and Unified Diffs |
| **File & Diff Viewer** | `src/viewer.c`, `src/source.c`, `src/layout.c`, `src/render.c`, `src/render_md.c`, `src/render_html.c`, `src/render_mermaid.c` | Dual-mode file viewer (Raw syntax-colored with line numbers & breadcrumb header vs. Rendered Markdown/HTML/Mermaid from `tam-view`) and unified diff viewer |
| **Split Panes & Tabs** | `src/pane.c` | Binary split-pane tree (`KNOX_SPLIT_V`, `KNOX_SPLIT_H`) inside each workbench tab, geometry layout, focus cycling, and hit-testing |
| **Workbench TUI** | `src/workbench.c` | Screen compositor, Sidebar (Source Control Repos + Changes / Explorer), Tab Bar, Command Palette & Chord Composer (`Ctrl+E`), Guidance Overlay (`Ctrl+G`), and `tam_key` event loop |

## Subprocess Safety

All Git invocations use `libtam`'s `tam_exec` / `tam_exec_opts` with
explicit `argv[]` arrays (never invoking `/bin/sh`), a bounded monotonic
timeout (15s for local queries, 60s for network fetch/pull/push), and
NUL-delimited output (`-z`) wherever paths are parsed so filenames with
spaces or unusual bytes cannot corrupt parsing.
