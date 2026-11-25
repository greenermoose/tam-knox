# P2: Branch Modal, Stash Management, and Side-by-Side Diff Split View (0.2.0)

- **Status:** Done (0.2.0, 2026-11-25)
- **Goal:** Add an interactive branch checkout and creation modal, a stash
  list management modal, and a side-by-side split diff mode to `tam-knox`.

## Versions

### 0.2.0

- Interactive Branch Checkout & Creation Modal (`Alt+K` or `c`):
  - Lists all local (`refs/heads`) and remote (`refs/remotes`) branches
    sorted with the current branch first, followed by local branches and
    remote tracking branches.
  - Supports live substring filtering across branch names and commit
    subjects.
  - `Enter` checks out a local branch or creates/switches to a local
    tracking branch for a remote ref (`git checkout --track`).
  - `Alt+N` prompts for a new branch name and creates + checks it out
    from `HEAD` (`git checkout -b <name>`).
- Stash List Management Modal (`Alt+Z` or `z`):
  - Lists all stashes (`git stash list`) with stash index (`stash@{N}`),
    originating branch, timestamp, and summary message.
  - `n` / `s` prompts for an optional stash message and saves working-tree
    and untracked changes (`git stash push -u -m <msg>`).
  - `Enter` inspects the full stash diff (`git stash show --stat -p -u`)
    inside the Diff Viewer.
  - `a` applies a stash (`git stash apply`), `p` pops a stash
    (`git stash pop`), and `d` / `x` drops a stash (`git stash drop`)
    after an explicit `y/N` confirmation prompt.
- Side-by-Side Diff Split View (`d`, `D`, `Alt+D`, or `R` in Diff Viewer):
  - Parses unified diff hunks (`@@ -old,+new @@`) into paired Old (left)
    and New (right) columns with dual line-number gutters and deletion /
    addition highlighting.
  - Supports synchronized vertical and horizontal scrolling and search
    (`/`, `n`, `N`) across both columns.

## Acceptance

- `make`, `make test`, and `make sanitize` pass with zero errors or
  memory leaks.
- `~/Code/tamlinux/tam-idol/tools/idol-check .` passes.
- Unit and PTY tests cover branch listing/creation/checkout, stash
  push/list/diff/apply/drop, and side-by-side diff hunk pairing and TUI
  toggling.

## Dependencies

- `libc` (POSIX.1-2008) and `libtam` 0.9.1+ (`libtam.a`). Zero external
  library dependencies.

## Risks

- **Accidental destructive stash drop:** Mitigated by requiring an
  explicit `y/N` confirmation prompt (`MODAL_CONFIRM_STASH_DROP`) before
  running `git stash drop`.

## Open points

- None for P2.
