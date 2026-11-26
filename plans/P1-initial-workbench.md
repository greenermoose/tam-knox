# P1: Initial Multi-Repository Workbench (0.1.0)

- **Status:** Done (0.1.0, 2026-11-25)
- **Goal:** Build `tam-knox` 0.1.0 in C17 on `libtam`, providing a
  VS Code-style multi-repository Source Control and Explorer sidebar,
  Git Graph commit history and diff inspector, and dual-mode File Viewer
  (raw syntax coloring + rendered Markdown/HTML/Mermaid) with tabs and
  split panes.

## Decisions (Fred, 2026-11-25)

1. **Workbench Layout:** VS Code Workbench style — left sidebar switches
   between Source Control (Repositories list + Changes list) and
   Explorer tree; right main area holds Tabs and Split Panes for Git
   Graph, File Viewer, and Commit/Diff details.
2. **Remote Sync (`1f`):** Runs `git fetch --prune`, then
   `git pull --ff-only` when behind, then `git push` when ahead, with an
   explicit confirmation prompt before pushing `main` or `test`.
3. **Additional Git & Editor Actions:**
   - View unified diff when selecting a changed file in a repo's dirty
     list or inside a commit in Git Graph.
   - Open the selected file in `tam-edit` (`Alt+E`).
   - Stage / unstage individual files and write commits directly inside
     `tam-knox` (scoped to staged paths).
4. **Syntax Coloring Engine:** Built-in lightweight C17 tokenizer lexer
   for common languages (C, Shell, Python, JS/TS/QML, JSON, YAML, TOML,
   INI, Makefile, Markdown, HTML, Rust, Go, Diff) plus `tam-view`'s
   Markdown/HTML/Mermaid renderer.

## Versions

### 0.1.0

- Repository scaffold following `tam-idol` 1.1.0 (`tools/idol-check`).
- Comprehensive field survey in `ideas/` (`vscode.md`,
  `vscode-git-graph.md`, `emacs-magit.md`, `lazygit.md`, `tig.md`,
  `gitui.md`, `synthesis.md`).
- Multi-repository discovery and single-call porcelain v2 status parser
  (`src/git.c`).
- Path-scoped stage/unstage, commit prompt, and remote sync with
  `main`/`test` confirmation (`src/git.c`, `src/workbench.c`).
- C17 port of `mhutchie.git-graph`'s `Vertex`/`Branch`/`determinePath`
  layout algorithm and Unicode/ASCII terminal rasterizer (`src/graph.c`).
- Hierarchical folder Explorer with live Git status badges
  (`src/explorer.c`).
- C17 syntax highlighter (`src/syntax.c`) and `tam-view` Markdown, HTML,
  and Mermaid viewer integration (`src/viewer.c`).
- Tab bar, recursive horizontal/vertical split-pane tree (`src/pane.c`),
  command palette & chord composer (`Ctrl+E`), and guidance overlay
  (`Ctrl+G`).

## Acceptance

- `make`, `make test`, and `make sanitize` pass with zero errors or
  leaks.
- `~/Code/tamlinux/tam-idol/tools/idol-check .` passes.
- Non-interactive `--status` and `--graph` modes and interactive PTY
  tests verify all four core user workflows.

## Dependencies

- `libc` (POSIX.1-2008) and `libtam` 0.9.1+ (`libtam.a`). Zero external
  library dependencies.

## Risks

- **Large workspace latency:** Running `git status` across 25+
  repositories on startup must feel fast. Mitigation: use single-call
  `git status --porcelain=v2 --branch -z` per repo and load full commit
  graphs lazily for the active repository.

## Open points

- None for P1.
