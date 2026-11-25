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
| `Alt+1` / `Backspace` | `Alt+1` / `Backspace` | `view-source-control` | Focus left sidebar in Source Control mode (`Backspace` returns from Explorer, syncing active repo) |
| `Alt+2` | `Alt+2` | `view-explorer` | Focus left sidebar in Explorer tree mode |
| `Enter` / `Right` (in Repos) | `Enter` / `Right` (in Repos) | `explore-repo` | Switch left sidebar to Explorer and expand the active repository |
| `Right` (in Explorer) | `Right` (in Explorer) | *(tree/viewer)* | Open closed folder, or open selected file in File Viewer and focus it |
| `Left` (Explorer / Viewer) | `Left` (Explorer / Viewer) | *(tree/viewer)* | Collapse open folder in Explorer, or move focus from File Viewer back to Explorer tree |
| `Alt+3` | `Alt+3` | `view-graph` | Focus or open the Git Graph tab for the active repository |
| `Alt+B` | `Alt+B` | `toggle-sidebar` | Show or hide the left sidebar |
| `Alt+V` | `Alt+V` | `split-vertical` | Split active main pane side-by-side (left / right) |
| `Alt+H` | `Alt+H` | `split-horizontal` | Split active main pane stacked (top / bottom) |
| `Alt+W` | `Alt+W` | `focus-other-pane` | Cycle focus among split panes inside the active tab |
| `Alt+,` / `Alt+.` | `Alt+,` / `Alt+.` | `tab-prev` / `tab-next` | Switch to previous / next tab in the main workbench |
| `f` | `f` | `repo-fetch` | Non-destructive `git fetch --prune` for the focused repository |
| `s`, `Alt+S` | `s`, `Alt+S` | `repo-sync` | Sync focused repository with remote (`fetch`, `pull --ff-only`, `push`; confirms on `main`/`test`) |
| `Alt+A`, `F` | `Alt+A`, `F` | `fetch-all` | Non-destructive batch `git fetch --prune` across all repositories with remotes |
| `Alt+C` | `Alt+C` | `commit-prompt` | Open commit message prompt to commit staged files in active repository |
| `c`, `Alt+K` | `c`, `Alt+K` | `branch-modal` | Open interactive Branch checkout & creation modal (`Alt+N` creates a new branch) |
| `z`, `Alt+Z` | `z`, `Alt+Z` | `stash-modal` | Open interactive Stash management modal (`n` push, `Enter` diff, `a` apply, `p` pop, `d` drop) |
| `d`, `D`, `Alt+D` | `d`, `D`, `Alt+D` | `toggle-diff-split` | Toggle Side-by-Side Split Diff vs. Unified Diff mode in Diff Viewer |
| `Alt+E` | `Alt+E` | `open-in-editor` | Open current file at current line in `tam-edit` (`$VISUAL` / `$EDITOR`) |
| `R` / `Alt+R` | `R` / `Alt+R` | `toggle-render-mode` | Toggle Rendered vs. Raw syntax-colored mode in File Viewer |
| `B` / `Alt+F` | `B` / `Alt+F` | `cycle-branch-filter` | Toggle Git Graph between All Branches and Current Branch (`HEAD`) |
| `Space` | `Space` | `stage-or-page-down` | Toggle stage/unstage on a file in Changes; scroll page down in File/Diff Viewer |
| `Enter` | `Enter` | `open-or-inspect` | Explore selected repo, expand/collapse Explorer folder, open file, or inspect commit details & changed files |
| `Escape` | `Escape` | `cancel-mode` | Close overlay/prompt, collapse commit details split, or clear search highlight |
| `]`, `[` | `]`, `[` | `link-next` / `link-prev` | Jump to next / previous hyperlink in rendered Markdown/HTML view |
| `b`, `PageUp` | `b`, `PageUp` | `page-up` | Scroll page up |
| `/`, `n`, `N` | `/`, `n`, `N` | `find`, `find-next`, `find-prev` | Search and step through matches |
| `g` | `g` | `guidance` | Toggle keybinding guidance overlay (alias for `Ctrl+G`) |
| `q` | `q` | `quit` | Quit `tam-knox` (when not typing in a prompt) |

## User `keymap.ini` Customization

Users can override or unbind any key by creating
`~/.config/tamlinux/tam-knox/keymap.ini` (respecting `$XDG_CONFIG_HOME`,
with fallback to `~/.config/tam-knox/keymap.ini` and optional
`TAM_KNOX_KEYMAP` environment variable override):

```ini
[keymap]
F5 = update
Alt+A = fetch-all
q = unbind
```

- Uses physical key names (`Ctrl+`, `Alt+`, `Shift+`, `F1`–`F12`, named
  navigation keys, and printable ASCII characters). `Meta+` chords are
  rejected because `Meta` belongs exclusively to `tam-pane`.
- Values must be canonical widget names from the tables above (plus
  `move-up`, `move-down`, `page-up`, `page-down`, `jump-top`,
  `jump-bottom`, `scroll-left`, `scroll-right`, `cancel-mode`, or
  `unbind`/`none`).
- Active overrides are shown with a `*` suffix in the Guidance overlay
  (`g` / `Ctrl+G`) and Command Palette (`Ctrl+E`).
