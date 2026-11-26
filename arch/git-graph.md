# Git Graph Layout and Rasterization (`src/graph.c`)

`tam-knox` ports the commit graph layout algorithm from the VS Code Git
Graph extension (`mhutchie.git-graph` 1.30.0, `media/out.min.js`) to C17
and rasterizes the resulting vector branch lines onto a terminal
character cell grid.

## 1. Data Structures

- **`knox_vertex_t` (row index `id = y`):**
  - `x`: assigned lane column (`0, 1, 2, ...`).
  - `parents[]`: indices of parent vertices in the loaded commit table
    (or `-1` for the sentinel `NULL_VERTEX` when a parent lies beyond the
    loaded commit window).
  - `children[]`: indices of child vertices.
  - `next_parent`: index into `parents[]` of the next unprocessed parent
    edge.
  - `branch_id`: index of the `knox_branch_t` that owns this vertex
    (`-1` if not yet placed on a branch).
  - `next_x`: lowest unoccupied lane index at row `y`.
  - `connections[]`: array indexed by column `x` recording `{connects_to,
    branch_id}` for every line passing through `(x, y)`.
  - `is_committed`, `is_current` (`HEAD`), `is_stash`.

- **`knox_branch_t`:**
  - `colour`: color palette index assigned via `get_available_colour(start_y)`.
  - `end_y`: row where this branch terminates (allowing its color index
    to be recycled by any branch starting at `y > end_y`).
  - `lines[]`: array of line segments `{p1(x1, y1), p2(x2, y2),
    is_committed, locked_first}` where `y2 == y1 + 1`.

## 2. `determine_path(g, start_y)`

While any vertex `y` from `0` to `N - 1` has an unprocessed parent
(`next_parent < parent_count`) or is not yet on a branch
(`branch_id == -1`):

1. Let `i = &g->vertices[start_y]`, `parent_id = get_next_parent(i)`.
2. Starting point `s = (i->branch_id == -1) ? (point){i->next_x, i->id} : (point){i->x, i->id}`.
3. **Merge Edge Coalescing:**
   If `parent_id >= 0`, `i` is a merge commit (`parent_count > 1`), and
   both `i` and `parent` are already placed on branches:
   - Step downward for `t = start_y + 1 .. N - 1` along
     `parent_branch = parent->branch_id`.
   - If `vertices[t]` already has a connection point routing to
     `(parent_id, parent_branch)`, connect `s -> r` into that point, mark
     the parent processed on `i`, and break.
   - Otherwise claim `r = (vertices[t].next_x, t)`, add segment `s -> r`
     to `parent_branch`, register `r.x` as unavailable on `vertices[t]`,
     and set `s = r`.
4. **Branch Path Tracing:**
   Otherwise allocate a new `knox_branch_t` with
   `colour = get_available_colour(g, start_y)`, place `i` on it at `s.x`
   (if `i->branch_id == -1`), mark `s.x` unavailable on `i`, and step
   downward for `t = start_y + 1 .. N - 1`:
   - If `parent_id == t` and `vertices[t].branch_id != -1`, target
     `r = (vertices[t].x, t)`; else `r = (vertices[t].next_x, t)`.
   - Add segment `s -> r` (`locked_first = (s.x < r.x)`), mark `r.x`
     unavailable on `vertices[t]`, and advance `s = r`.
   - When `parent_id == t`:
     mark parent processed on `i`; note `already_on_branch = (vertices[t].branch_id != -1)`;
     place `vertices[t]` on this branch if not yet placed; advance
     `i = &vertices[t]`, `parent_id = get_next_parent(i)`; and break if
     `parent_id == -1` or `already_on_branch`.

## 3. Terminal Cell Rasterization

Each lane `x` occupies 2 terminal character columns (`2*x` for the lane
center and `2*x + 1` for inter-lane horizontal/diagonal connectors):

- **Commit node at `(2 * v->x)`:**
  - `HEAD` commit: `◎` (Unicode) / `@` (ASCII)
  - Uncommitted changes: `◌` (Unicode) / `o` (ASCII)
  - Merge commit: `◆` (Unicode) / `M` (ASCII)
  - Normal commit: `●` (Unicode) / `*` (ASCII)
- **Vertical continuation (`x1 == x2`):** `│` (Unicode) / `|` (ASCII) at
  column `2 * x1`.
- **Branch-out / Merge-in (`x1 != x2`):** draws horizontal span `─` (`-`)
  between `2 * min(x1, x2)` and `2 * max(x1, x2)` with rounded corners
  `╭`, `╮`, `╰`, `╯` (or `/` and `\` in ASCII) and junction tees
  `├`, `┤`, `┼` (`+` in ASCII) where horizontal connectors meet vertical
  lanes.
