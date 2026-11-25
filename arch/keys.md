# tam-knox Key Bindings

`tam-knox` follows the Tamlinux-wide keybinding standard in
`tamlinux/arch/keybindings.md`. Every `Ctrl` chord retains its single
universal Tamlinux meaning, no binding uses `Meta` (reserved for
`tam-pane`), and no binding uses `Alt+Left/Right` or `Alt+F1..F12`
(captured by the Linux kernel VT driver on `TERM=linux`).

## Universal `Ctrl` Map

| Key | Canonical Widget | Behavior in `tam-knox` |
| --- | --- | --- |
| `Ctrl+A` | `select-all` | Select all text in commit/search prompt |
| `Ctrl+B` | *(reserved)* | No-op |
| `Ctrl+C` | `copy` | Copy active selection, focused commit hash, or file path (OSC 52 write + internal clipboard) |
| `Ctrl+D` | `close` | Close active split pane (if split) or active workbench tab |
| `Ctrl+E` | `execute` | Open Command Palette & One-Handed Chord Composer (`Tab` cycles modifiers on `TERM=linux`) |
| `Ctrl+F` | `find` | Open search bar to filter repositories, search commits in Git Graph, or find in File/Diff Viewer |
| `Ctrl+G` | `guidance` | Toggle full keybinding guidance overlay |
| `Ctrl+K` | `kill-line` | Clear current prompt input buffer |
| `Ctrl+L` | `clear-screen` | Clear and redraw terminal screen |
| `Ctrl+N` | `new` | Open a new tab in the main workbench |
| `Ctrl+O` | `open` | Focus repository / file quick switcher |
| `Ctrl+P` | `pause` | Suspend `tam-knox` (`SIGTSTP`) and restore terminal modes cleanly |
| `Ctrl+Q` | `quit` | Quit `tam-knox` |
| `Ctrl+R` | `replace` | No-op (viewer surfaces are read-only; `Alt+E` opens `tam-edit`) |
| `Ctrl+S` | `save` | Commit staged changes when in commit prompt or Source Control Changes |
| `Ctrl+T` | *(reserved)* | No-op |
| `Ctrl+U` | `update` | Refresh repository statuses, Git Graph, Explorer tree, and open file buffers |
| `Ctrl+V` | `paste` | Paste clipboard into commit message or search prompt |
| `Ctrl+W` | *(reserved)* | No-op |
| `Ctrl+X` | `cut` | Cut prompt text to clipboard |
| `Ctrl+Y` | `redo` | Redo prompt edit |
| `Ctrl+Z` | `undo` | Undo prompt edit |
| `Ctrl+\` | `break` | Cancel active prompt or modal overlay |

## Workbench, Sidebar, Tabs, Split Panes, and Git Actions

| Key | `TERM=linux` / Compact | Canonical Widget | Behavior |
| --- | --- | --- | --- |
| `Tab` / `Shift+Tab` | `Tab` / `Alt+I` | `focus-next` / `focus-prev` | Cycle focus across Sidebar (Repos -> Changes / Explorer) and Main Panes |
| `Alt+1` | `Alt+1` | `view-source-control` | Focus left sidebar in Source Control mode (Repositories + Changes) |
| `Alt+2` | `Alt+2` | `view-explorer` | Focus left sidebar in Explorer tree mode |
| `Alt+3` | `Alt+3` | `view-graph` | Focus or open the Git Graph tab for the active repository |
| `Alt+B` | `Alt+B` | `toggle-sidebar` | Show or hide the left sidebar |
| `Alt+V` | `Alt+V` | `split-vertical` | Split active main pane side-by-side (left / right) |
| `Alt+H` | `Alt+H` | `split-horizontal` | Split active main pane stacked (top / bottom) |
| `Alt+W` | `Alt+W` | `focus-other-pane` | Cycle focus among split panes inside the active tab |
| `Alt+,` / `Alt+.` | `Alt+,` / `Alt+.` | `tab-prev` / `tab-next` | Switch to previous / next tab in the main workbench |
| `Alt+S` | `Alt+S` | `repo-sync` | Sync focused repository with remote (`fetch`, `pull --ff-only`, `push`; confirms on `main`/`test`) |
| `Alt+C` | `Alt+C` | `commit-prompt` | Open commit message prompt to commit staged files in active repository |
| `Alt+E` | `Alt+E` | `open-in-editor` | Open current file at current line in `tam-edit` (`$VISUAL` / `$EDITOR`) |
| `R` / `Alt+R` | `R` / `Alt+R` | `toggle-render-mode` | Toggle Rendered vs. Raw syntax-colored mode in File Viewer |
| `B` / `Alt+F` | `B` / `Alt+F` | `cycle-branch-filter` | Toggle Git Graph between All Branches and Current Branch (`HEAD`) |
| `Space` | `Space` | `stage-or-page-down` | Toggle stage/unstage on a file in Changes; scroll page down in File/Diff Viewer |
| `Enter` | `Enter` | `open-or-inspect` | Select repo, expand/collapse Explorer folder, open file, or inspect commit details & changed files |
| `Escape` | `Escape` | `cancel-mode` | Close overlay/prompt, collapse commit details split, or clear search highlight |
| `]`, `[` | `]`, `[` | `link-next` / `link-prev` | Jump to next / previous hyperlink in rendered Markdown/HTML view |
| `b`, `PageUp` | `b`, `PageUp` | `page-up` | Scroll page up |
| `/`, `n`, `N` | `/`, `n`, `N` | `find`, `find-next`, `find-prev` | Search and step through matches |
| `q` | `q` | `quit` | Quit `tam-knox` (when not typing in a prompt) |
