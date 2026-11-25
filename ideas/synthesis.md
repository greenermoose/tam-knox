# The best ideas, by topic

This file gathers the best ideas from every surveyed program in this
folder ([`vscode.md`](vscode.md), [`vscode-git-graph.md`](vscode-git-graph.md),
[`emacs-magit.md`](emacs-magit.md), [`lazygit.md`](lazygit.md),
[`tig.md`](tig.md), and [`gitui.md`](gitui.md)), grouped by how
`tam-knox` uses them.

## 1. Workbench Layout: Sidebar + Tabbed & Split Main Area

- **VS Code Workbench architecture (`vscode.md`).** A toggleable left
  sidebar (`Alt+1` Source Control, `Alt+2` Explorer, `Alt+B` hide/show)
  paired with a right main workbench featuring a **Tab Bar** across the
  top and a **Binary Split-Pane Tree** (`Alt+V` vertical split, `Alt+H`
  horizontal split, `Alt+W` cycle pane, `Ctrl+D` close pane/tab). P1 0.1.0.
- **Repo-disambiguated tab titles (`vscode.md`).** Tabs show the file or
  view title plus the repository name and status badge when needed (for
  example, `Git Graph [tam-clam]`, `ROADMAP.md [tam M]`). P1 0.1.0.
- **Contextual footer and guidance (`gitui.md`, `emacs-magit.md`).**
  Bottom status bar shows active focus, position, last Git action status,
  and contextual key hints, backed by `Ctrl+G` (Guidance overlay) and
  `Ctrl+E` (Command Palette & Chord Composer). P1 0.1.0.

## 2. Multi-Repository Source Control & Safe Remote Sync

- **Single-glance repository table (`vscode.md`, `emacs-magit.md`).**
  Lists every repository in the workspace folder with its name, current
  branch, dirty indicator (`*` + count of changed files), and ahead/behind
  badges (`↑N ↓M`). Also available non-interactively via
  `tam-knox --status [dir]`. P1 0.1.0.
- **Single-call porcelain v2 status (`src/git.c`).** Calling
  `git -C <repo> status --porcelain=v2 --branch -z` extracts the commit
  OID, branch name, upstream branch, ahead/behind counts, and all staged,
  unstaged, renamed, and untracked files in one fast subprocess via
  `libtam`'s `tam_exec`. P1 0.1.0.
- **Changes list with path-scoped staging and committing (`vscode.md`,
  `emacs-magit.md`).** The bottom half of the Source Control sidebar lists
  the selected repo's changed files (`M`, `A`, `D`, `R`, `U`). `Space`
  stages or unstages a file, `Enter` views its unified diff, and `Alt+C`
  commits only the explicitly staged paths (`git commit -m ... -- <paths>`).
  P1 0.1.0.
- **Stage-aware remote sync (`vscode.md`, `gitui.md`).** Pressing `Alt+S`
  runs `git fetch --prune`, fast-forward pulls (`git pull --ff-only`)
  when behind, and pushes (`git push`) when ahead, pausing for explicit
  confirmation if the checked-out branch is `main` or `test`. P1 0.1.0.

## 3. Git Graph Commit History & Commit Inspector

- **`mhutchie.git-graph`'s `Vertex` / `Branch` / `determinePath` layout
  engine (`vscode-git-graph.md`).** Computes non-colliding branch lanes,
  coalesces merge edges bound for the same parent lane, and recycles
  branch colors as branches end. P1 0.1.0 (`src/graph.c`).
- **2-cell-per-lane terminal rasterizer (`tig.md`, `vscode-git-graph.md`).**
  Renders smooth Unicode box-drawing connectors (`│`, `─`, `╭`, `╮`, `╰`,
  `╯`, `├`, `┤`, `┼`, `●`, `◎`, `◌`) in `tam-last` and clean ASCII (`|`,
  `-`, `/`, `\`, `*`, `o`) on `TERM=linux` or with `--ascii`. P1 0.1.0.
- **Inline ref badges and synthetic `Uncommitted Changes (N)` row
  (`vscode-git-graph.md`).** Shows colored `[HEAD -> dev]`, `[origin/dev]`,
  and `[tag: ...]` pills before the commit subject, plus row 0 for dirty
  working trees. P1 0.1.0.
- **Split commit details & changed-files inspector (`vscode-git-graph.md`,
  `tig.md`).** Pressing `Enter` on any commit opens a split inspector
  showing full commit metadata, commit body, and every changed file with
  `+add / -del` stats (`git diff-tree --name-status` + `--numstat`),
  where selecting a file opens its unified diff or historical file
  snapshot. P1 0.1.0.

## 4. Folder Explorer with Git Status Decorations

- **Collapsible workspace tree with live Git badges (`vscode.md`).**
  Displays folders and files across the workspace, decorating modified
  files with right-aligned colored `M`/`A`/`D`/`R`/`U` badges and parent
  directories containing dirty descendants with a colored indicator dot.
  P1 0.1.0 (`src/explorer.c`).

## 5. File & Diff Viewer (`tam-view` + Syntax Highlighting)

- **`tam-view` Markdown, HTML, and Mermaid rendering (`UPSTREAM.md`).**
  Reuses `tam-view`'s C11 rendering engine so `.md`, `.html`, and `.mmd`
  files render formatted headings, tables, blockquotes, lists, hyperlinks,
  and Mermaid diagrams, toggled with `R` / `Alt+R`. P1 0.1.0.
- **Built-in C11 table-driven syntax lexer (`src/syntax.c`).** Colors C/C++,
  POSIX Shell, Python, JS/TS/QML, JSON/JSONC, YAML, TOML, INI, Makefile,
  Markdown, HTML, Rust, Go, and Unified Diffs with line numbers and a
  breadcrumb bar (`repo > file > heading`), plus `Alt+E` to jump into
  `tam-edit` at the current line. P1 0.1.0.
