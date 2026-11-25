# VS Code Git Graph (`mhutchie.git-graph`)

## What it is

- **Language:** TypeScript (compiled to Node.js CommonJS in `out/` and
  minified webview JS/SVG in `media/out.min.js`). **License:** MIT
  (Copyright 2018-2021 Michael Hutchison).
- **Scope:** The Visual Studio Code extension (`mhutchie.git-graph`,
  v1.30.0) that renders an interactive commit graph, branch/tag/remote
  pills, uncommitted changes, and per-commit file-change details.
- **Depth:** Direct source-code reading of the installed extension at
  `mhutchie.git-graph-1.30.0/out/dataSource.js`, `out/repoManager.js`,
  `out/gitGraphView.js`, and `media/out.min.js`.

## How Git Graph works (from source inspection)

### 1. Repository Discovery (`out/repoManager.js`)

`RepoManager.searchDirectoryForRepos(directory, maxDepth)` checks
whether `directory` itself is a Git root; if not and `maxDepth > 0`, it
enumerates child directories (skipping `.git`) and recurses with
`maxDepth - 1`. Known repositories are keyed by canonical root path.

### 2. Git Data Ingestion (`out/dataSource.js`)

Git Graph runs `git` directly via `child_process.spawn` (no shell) using
a long random field separator (`GIT_LOG_SEPARATOR`):

- **Commit log (`getLog`):**
  ```text
  git -c log.showSignature=false log --max-count=<N+1>
      --format=%H<sep>%P<sep>%an<sep>%ae<sep>%at<sep>%s
      --topo-order --branches --tags --remotes HEAD --
  ```
  Asking for `maxCommits + 1` detects whether more commits exist without
  a second count query.
- **Refs (`getRefs`):**
  ```text
  git show-ref -d --head
  ```
  Categorizes `HEAD`, `refs/heads/<name>`, `refs/tags/<name>` (handling
  `^{}` dereferenced annotated tags so tags attach to the commit hash
  rather than the tag object hash), and `refs/remotes/<remote>/<branch>`.
- **Uncommitted changes node (`getCommits`):**
  When `refData.head` matches a loaded commit and
  `git status --untracked-files=all --porcelain` has `k > 0` lines, Git
  Graph prepends a synthetic commit at index 0:
  `{ hash: "*", parents: [refData.head], author: "*", message: "Uncommitted Changes (k)" }`.
- **Commit details and changed files (`getCommitDetails`):**
  Combines:
  1. `git -c log.showSignature=false show --quiet <hash> --format=...`
     for full author/committer metadata and `%B` body,
  2. `git diff-tree --name-status -r --root --find-renames --diff-filter=AMDR -z <hash>`
     (or `git diff` between two commits / working tree), and
  3. `git diff-tree --numstat -r --root --find-renames --diff-filter=AMDR -z <hash>`
     to merge `+additions` and `-deletions` counts with `A`/`M`/`D`/`R`/`U`
     file change records (`generateFileChanges`).

### 3. Commit Graph Layout Algorithm (`media/out.min.js`)

In `media/out.min.js`, three classes compute the graph layout:
`Branch` (`De`), `Vertex` (`Pe`), and `Graph` (`Me`):

1. **Vertex Initialization (`Graph.loadCommits`):**
   - Creates one `Vertex(id = y)` per commit row `y = 0..N-1`, plus a
     sentinel `nullVertex` (`id = -1`) representing parents outside the
     loaded window.
   - For each commit `y` and each parent hash `p`:
     - If `p` is in `commitLookup`, links `vertices[y].addParent(vertices[idx])`
       and `vertices[idx].addChild(vertices[y])`.
     - Otherwise (if `!onlyFollowFirstParent || r === 0`), links
       `vertices[y].addParent(nullVertex)`.
   - Loops from `y = 0` while `y < vertices.length`:
     if `vertices[y].getNextParent() !== null` or
     `vertices[y].isNotOnBranch()`, calls `determinePath(y)`; otherwise
     increments `y`.

2. **Lane Assignment & Edge Routing (`Graph.determinePath(startAt)`):**
   - Let `i = vertices[startAt]`, `parent = i.getNextParent()`.
   - Start point `s = i.isNotOnBranch() ? i.getNextPoint() : i.getPoint()`.
   - **Case A (Merge into an already-placed parent branch):**
     When `parent != null && parent.id != -1 && i.isMerge() && !i.isNotOnBranch() && !parent.isNotOnBranch()`:
     - Traces downward row-by-row (`t = startAt + 1 .. N - 1`) on
       `parentBranch = parent.getBranch()`.
     - At row `t`, checks if `vertices[t]` already has a connection cell
       routing to `(parent, parentBranch)` via
       `getPointConnectingTo(parent, parentBranch)`.
     - If found, connects into that point and stops early (avoiding
       duplicate parallel lines to the same merge target!).
     - Otherwise claims `vertices[t].getNextPoint()`, adds the line segment
       to `parentBranch`, and calls
       `vertices[t].registerUnavailablePoint(r.x, parent, parentBranch)`.
   - **Case B (New branch / trunk path):**
     - Allocates a new `Branch(colour = getAvailableColour(startAt))`.
     - Places `i` on that branch if not already placed
       (`i.addToBranch(branch, s.x)`) and marks `s.x` unavailable at row
       `i`.
     - Steps downward for `t = startAt + 1 .. N - 1`:
       - At row `t`, if `parent == vertices[t]` and `parent` is already
         on a branch, target point `r = vertices[t].getPoint()`;
         otherwise `r = vertices[t].getNextPoint()`.
       - Adds segment `(s -> r)` with `lockedFirst = (s.x < r.x)` to
         `branch`, and registers `r.x` as unavailable on `vertices[t]`
         (connecting to `parent` on `branch`).
       - When `parent == vertices[t]`: marks parent processed on `i`; if
         `parent` was not yet on a branch, places `parent` on `branch` at
         `r.x`, advances `i = parent`, `parent = i.getNextParent()`, and
         continues the same branch downward! Stops when `parent == null`
         or `parent` was already on an existing branch.
     - Records `branch.setEnd(t)` and updates
       `availableColours[branch.colour] = t` so colors recycle once a
       branch terminates.

## Ideas worth taking

- **The exact `Vertex` / `Branch` / `determinePath` lane-routing and
  color-recycling algorithm.** It produces compact, planar, non-colliding
  commit graphs where merge edges coalesce into existing lanes bound for
  the same parent and branch colors recycle cleanly.
  **adopt (P1, 0.1.0, `src/graph.c`)**.
- **Top control bar with `Repo:`, `Branches:` filter, and `Remote`
  toggle above columns `Graph | Description | Date | Author | Commit`.**
  Matches Fred's daily mental model from VS Code. **adopt (P1, 0.1.0)**.
- **Inline ref badges on the commit description column.** Showing
  `[HEAD -> dev]` / `[origin/dev]` / `[tag: v0.1.0]` in distinct colors
  right before the commit subject makes divergence immediately visible.
  **adopt (P1, 0.1.0)**.
- **Synthetic `Uncommitted Changes (N)` node at row 0 when dirty.** Lets
  the user inspect working-tree changes directly from the top of the
  graph as well as from the Source Control sidebar. **adopt (P1, 0.1.0)**.
- **Single-command `git show-ref -d --head` + `--topo-order` log
  fetch.** Fast, deterministic, and handles annotated tag dereferencing
  (`^{}`). **adopt (P1, 0.1.0, `src/git.c`)**.

## Things to avoid

- **SVG DOM rendering.** In a terminal, we rasterize `Branch.lines` and
  `Vertex` positions directly into a character cell grid (2 cells wide
  per lane: column `2*x` for the lane and `2*x + 1` for horizontal/diagonal
  connectors) using Unicode box-drawing (`│`, `─`, `╭`, `╮`, `╰`, `╯`,
  `├`, `┤`, `┼`, `●`, `◎`, `◌`) and ASCII fallback (`|`, `-`, `/`, `\`,
  `*`, `o`).

## Where to look

- `out/dataSource.js` (`getCommits`, `getLog`, `getRefs`,
  `getCommitDetails`, `getDiffNameStatus`, `getDiffNumStat`).
- `media/out.min.js` (`De` / `Branch`, `Pe` / `Vertex`, `Me` / `Graph`,
  `loadCommits`, `determinePath`, `getAvailableColour`).
