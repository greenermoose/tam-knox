# P3: File Tree Filtering, Multi-Repo Batch Fetch, and User `keymap.ini` (0.3.0)

- **Status:** Done (0.3.0, 2026-11-25)
- **Goal:** Add live substring filtering to the Explorer tree, non-destructive
  multi-repository batch fetch (`Alt+A` / `F`), and optional user `keymap.ini`
  customization powered by `libtam`'s `tam_ini` parser.

## Versions

### 0.3.0

- Explorer File Tree Substring Filter (`/` or `Ctrl+F` in Explorer, `Esc` to clear):
  - Live case-insensitive substring matching across filenames and workspace-relative
    paths (`knox_explorer_set_filter` / `knox_explorer_clear_filter`).
  - Automatically descends into collapsed directories up to depth 5 (capped at
    4000 visited nodes, skipping `build/`, `node_modules/`, `worktrees/`, `.git/`)
    while preserving each directory's original `expanded` state so `Esc` restores
    the prior tree cleanly.
  - Refreshes live Git status badges on newly loaded directories during filtering.
- Multi-Repo Batch Fetch (`Alt+A` or `F`, widget `fetch-all`):
  - Runs `git fetch --prune` across every discovered repository with a configured
    remote (`knox_repo_fetch` / `knox_workspace_fetch_all`) with a 30-second
    per-repository timeout.
  - Strictly non-destructive: never pulls, merges, or pushes; skips local-only
    repositories without remotes and updates ahead/behind (`↑N ↓M`) counts.
  - Displays live per-repository progress in the status bar and summarizes
    fetched, skipped, failed, and behind-upstream counts on completion.
- Optional User `keymap.ini` Configuration (`src/keymap.c`):
  - Uses `libtam`'s `tam_ini_parse_file` / `tam_ini_parse_string` and
    `tam_xdg_path(TAM_XDG_CONFIG, "tam-knox", "keymap.ini", false)`
    (`~/.config/tamlinux/tam-knox/keymap.ini`, with `~/.config/tam-knox/keymap.ini`
    fallback and `TAM_KNOX_KEYMAP` environment override).
  - Supports `[keymap]` (plus `[bindings]`, `[keys]`, or top-level) entries of
    the form `KeySpec = widget-name` or `unbind` / `none`, using physical key
    names (`Ctrl+`, `Alt+`, `Shift+`, `F1`–`F12`, named nav keys, printable chars).
  - Rejects `Meta+` chords per `tam-idol/keys/keybindings.md`, warns on invalid
    keys or unknown widget names without aborting startup, and reflects custom
    bindings (`Key*`) in the Guidance overlay (`g` / `Ctrl+G`) and Command
    Palette (`Ctrl+E`).

## Acceptance

### 1.1.3 guidance and update

- Guidance opens in key order at startup. `Tab`, `Left`, and `Right` switch
  between key order and command-name order inside guidance.
- `u` and `Ctrl+U` use the same update command. Open file views reload
  across tabs and splits. A failed reload marks that view stale until a
  successful refresh.

### P3 checks

- `make`, `make test`, and `make sanitize` pass with zero errors or leaks.
- `~/Code/tamlinux/tam-idol/tools/idol-check .` passes.
- Unit tests (`tests/test-p3.c`) and PTY tests (`tests/test-pty.c`) cover
  Explorer substring filtering, multi-repo batch fetch, and `keymap.ini`
  parsing, validation, and runtime dispatch.

## Dependencies

- `libc` (POSIX.1-2008) and `libtam` 0.9.1+ (`libtam.a`, including `tam_ini`
  and `tam_xdg_path`). Zero external library dependencies.

## Risks

- **Large workspace trees during live Explorer filtering:** Mitigated by
  bounding auto-descent to depth 5 and 4000 nodes while skipping `build/`,
  `node_modules/`, `worktrees/`, and `.git/`.
- **Slow or unreachable remotes during batch fetch:** Mitigated by skipping
  remote-less repos via a local `git remote` check and enforcing a 30-second
  per-repository monotonic timeout via `tam_exec`.

## Open points

- None for P3.
