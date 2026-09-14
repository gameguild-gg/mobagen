# Maze Generator assignment

You are a game developer tasked with implementing a **deterministic maze generator**: given a size and a seed, it must always produce the exact same maze. Determinism is what makes it testable — and gradeable.

Everything happens in **this one repo**:

- **Formal assignment:** the automated tests live here. The fixtures are `.in`/`.out` pairs in `apps/maze/tests/`, replayed by the `maze-tests` runner. Your implementation surface is the `RecursiveBacktrackerExample` generator (`apps/maze/generators/RecursiveBacktrackerExample.cpp`): `Clear`, `Step` and `getVisitables`, working on the same `World` grid the demo app renders. The runner seeds `SeededRandom` (`apps/maze/SeededRandom.h`) with the fixture's index, steps your generator, and renders the `World` walls as ascii.
- **Interactive assignment:** the `maze` demo app renders maze generators live (`apps/maze/generators/`), stepping them on a `World` grid — a different surface, driven by animations rather than seeds. Doing well on the formal part teaches you the algorithm you will then implement there.

::: tip "Your edit and test loop"

From the repo root:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --parallel --target maze-tests
./build/bin/maze-tests
```

Edit the solution regions in the generator, rebuild, rerun. That is the whole loop.

The report prints one line per fixture, `[fixture] <name> PASS` or `REJECT`, then a summary such as `Maze formal tests: 6/8 passed (75.0%)` followed by grade-ready counts (`Passed: 6`, `Failed: 2`). The exit code stays nonzero until every fixture passes. A CI workflow publishes the same report with the counts on every push that touches `apps/maze/`.

:::

::: warning "Determinism is the whole assignment"

Randomness comes ONLY from the fixed `randomNumbers` array in `SeededRandom.h`, seeded by `SeededRandom::setIndex(randomIndex)` and consumed strictly in order. Every `SeededRandom::next()` call shifts the entire rest of the generation, so a maze that looks right but consumed randomness differently still REJECTs. Follow the procedure below exactly.

:::

::: note "Two coordinate systems"

The generator's algorithm runs in **formal units**: `(0, 0)` is the top-left cell, x grows right, y grows down. The `World` stores cells in **centered units** around its middle. `World::ToWorldCoords` and `World::ToFormalCoords` translate between the two — the hints use formal units and translate only when touching the grid.

:::

## Input

Three numbers: `width`, `height` and `randomIndex` — the maze dimensions and the seed (the starting index into the fixed random array).

```text
5 5 0
```

When you run `maze-tests`, the runner feeds inputs and reads outputs in exactly this format, straight from the fixture files in `apps/maze/tests/`; you never type anything into stdin.

## The generation procedure

A recursive backtracker over the grid, starting at cell `(0, 0)`:

1. Mark the current cell as visited and list all its visitable (unvisited) neighbors in clockwise order starting from the top: UP, RIGHT, DOWN, LEFT;
2. If there is exactly one visitable neighbor, move to it — do **not** consume a random number;
3. If there are two or more, consume `Random::next()` and move to the neighbor at `next() % visitableCount`;
4. If there are none, backtrack (pop the path stack);
5. Moving to a neighbor removes the wall between the two cells — translate the formal cell with `World::ToWorldCoords` first;
6. The maze is complete when the path stack empties.

`getVisitables`'s hint lists the neighbors already in the required order.

## Output

The maze rendered as ascii: the first line is the top border (one `_` per column), then one line per row of cells with `|` for walls, `_` for walls below a cell, and spaces for open passages. Trailing whitespace is ignored by the tests.

```text
 _ _ _ _ _
| |_ _ _  |
|_ _ _  | |
|  _  | | |
|_  | | | |
|_ _|_ _ _|
```

## Grading

10 points total:

- 5 Points – passed on the formal test cases;
- 3 Points – deterministic generation implemented exactly per the procedure;
- 2 Points – code quality and properly submitted in Canvas.

## References

- [Maze generation algorithms](https://en.wikipedia.org/wiki/Maze_generation_algorithm) — recursive backtracker is also known as randomized depth-first search
