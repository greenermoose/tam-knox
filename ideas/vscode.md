# Visual Studio Code (Source Control, Explorer, and Workbench)

## What it is

- **Language:** TypeScript / JavaScript on Electron. **License:** MIT
  (`microsoft/vscode`).
- **Scope:** Graphical code editor and multi-repository workspace
  workbench. Fred previously used VS Code to monitor all `~/Code/tamlinux`
  repositories in a single window, inspect dirty files, sync with
  remotes, browse folder trees, and view files alongside the Git Graph
  extension.
- **Depth:** Source and live-UI study of VS Code's multi-root Source
  Control view (`src/vs/workbench/contrib/scm/`), Git extension
  (`extensions/git/`), Explorer (`contrib/files/`), and Editor Groups /
  Tabs layout.

## Ideas worth taking

- **Two-mode left sidebar (Source Control vs. Explorer) paired with a
  tabbed/split main editor area.** Switching the left sidebar between
  Source Control (`Alt+1`) and Explorer (`Alt+2`) while keeping open
  tabs (`Git Graph`, `README`, `ROADMAP.md`) in the right main area gives
  maximum horizontal space to the commit graph and file viewer.
  **adopt (P1, 0.1.0)**.
- **Multi-repository Source Control Repositories list.** One compact row
  per repository showing:
  1. Repository name (`tam-clam`, `libtam`, `tam`),
  2. Checked-out branch name (`dev`, `main`),
  3. Dirty asterisk / count (`dev*`),
  4. Ahead/behind counts relative to upstream (`↑1 ↓2`), and
  5. One-action remote sync (`git fetch`, `git pull --ff-only`,
     `git push`).
  **adopt (P1, 0.1.0)**.
- **Contextual Changes section below the Repositories list.** Selecting a
  repository in the top half of the Source Control sidebar immediately
  lists its staged and working-tree changed files (`M`, `A`, `D`, `R`,
  `U`) in the lower half, where pressing `Space` stages/unstages a file,
  `Enter` opens its unified diff in the main area, and `Alt+C` opens the
  commit message box. **adopt (P1, 0.1.0)**.
- **Explorer tree with right-aligned Git status badges and folder dirty
  indicators.** Every modified or untracked file in the Explorer tree
  carries a colored right-aligned badge (`M`, `U`, `A`, `D`), and any
  collapsed or expanded parent folder containing dirty descendants shows
  a colored dot (`●` / `*`). **adopt (P1, 0.1.0)**.
- **Tab disambiguation by repository name.** When files with the same
  basename are open from different repositories (for example,
  `ROADMAP.md tam M` and `ROADMAP.md tam-clam`), the tab title appends
  the repository name and dirty status letter. **adopt (P1, 0.1.0)**.
- **Breadcrumb header in the file viewer.** Showing
  `repo > path/to/file > current section heading` at the top of the file
  viewer orients the user immediately inside large repositories.
  **adopt (P1, 0.1.0)**.
- **Minimap gutter.** **reject**: wastes 10-15 terminal columns that are
  much better spent on readable text or a side-by-side split pane.

## Things to avoid

- **Electron / Chromium memory and CPU footprint.** Running a full
  browser engine to inspect 25 Git repositories and Markdown files uses
  hundreds of megabytes of RAM and background file-watcher daemons.
  `tam-knox` runs in a few megabytes of C11 in `tam-last` or a Linux VT.
- **Unscoped "Commit All" defaults.** When no files are staged, VS Code
  offers to stage and commit the entire working tree (`git commit -a`),
  which violates Tamlinux's multi-agent path-claim rules. `tam-knox`
  commits only explicitly staged file paths (`git commit -m ... -- <paths>`).
- **Silent push of protected branches.** Clicking Sync in VS Code pushes
  whatever branch is checked out without distinguishing `dev` from
  `main` or `test`. `tam-knox` prompts for explicit confirmation before
  pushing `main` or `test`.

## Where to look

- `extensions/git/src/repository.ts` and `model.ts` in `microsoft/vscode`
  for multi-repo discovery and `git status` parsing.
- `src/vs/workbench/contrib/scm/browser/scmRepositoriesViewPane.ts` and
  `scmViewPane.ts` for the Repositories + Changes split sidebar.
