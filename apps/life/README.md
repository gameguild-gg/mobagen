# Game of Life assignment

You are applying for an internship position at Valvule Corp, and they want to test your abilities to manage states. You were tasked to code the Conway's Game of Life.

Everything happens in **this one repo**. For the formal assignment you do not clone anything else:

- **Formal assignment:** the automated tests live here. The fixtures are `.in`/`.out` pairs in `apps/life/tests/`, replayed by the `life-tests` runner. Your implementation surface is the same rule class the demo app uses: `apps/life/rules/JohnConway.cpp` (conditions, actions, wiring, `Step` and `CountNeighbors`) on top of the `World` grid (`apps/life/World.h`) and the `fsm/` framework (`apps/life/fsm/`), where you implement the machine core (`StateMachine::Update`). There is no separate formal header to fill in.
- **Interactive assignment:** the `life` demo app in this repo renders your rule live (and lets you explore other tile sets, such as the hexagonal variant in `apps/life/rules/HexagonGameOfLife.cpp`). You can still code in any language and/or game engine you want, but working here is the shortest path.

::: tip "Your edit and test loop"

From the repo root:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --parallel --target life-tests
./build/bin/life-tests
```

Edit the solution regions (marked `begin solution` / `end solution`), rebuild, rerun. That is the whole loop.

The report prints one line per fixture, `[fixture] <name> PASS` or `REJECT`, then a summary such as `Life formal tests: 12/13 passed (92.3%)`. The exit code stays nonzero until every fixture passes, and the failing ones are listed again under `Rejected:`. A CI workflow publishes the same report on every push that touches `apps/life/`.

:::

::: note "Which rules apply where?"

The exact cell semantics and the console input/output format below are **required for the formal assignment** (this repo, checked by the `life-tests` runner). For the **interactive assignment** (this boilerplate or any engine) you have freedom: keep the core Game of Life intent, but extra rules, tile sets and visualizations are up to you.

:::

::: warning "Double buffering"

`World` keeps two buffers: read the current state with `Get`, write the next state with `SetNext`, and never mix them inside a generation. The flip (`SwapBuffers`) is **not your job**: the driver — `Manager::step` in the demo app, or the `life-tests` runner — calls it right after `Step` returns. Your actions write `SetNext` only; computing a generation in place is the classic way to corrupt the simulation.

:::

The [ai4games repo](https://github.com/gameguild-gg/ai4games/) is the original home of this assignment. Keep it as a historical reference only; everything you need is here.

## The rules

The game consists in a C x L matrix of cells (Columns and Lines), where each cell can be either alive or dead. The game is played in turns, where each turn the state of the cells are updated according to the following rules:

1. Any live cell with fewer than two live neighbours dies, as if by underpopulation.
2. Any live cell with two or three live neighbours lives on to the next generation.
3. Any live cell with more than three live neighbours dies, as if by overpopulation.
4. Any dead cell with exactly three live neighbours becomes a live cell, as if by reproduction.

The map is continuous on every direction, so the cells on the edges have the cells on the opposite edge as neighbors. It is effectively a toroidal surface. The provided `World` already wraps coordinates around for you.

## The state machine way

Each cell is an agent running a tiny two-state machine, and the four rules above become machine parts. This is the classical design from Millington's *AI for Games* (ch. 5) — the same vocabulary as the [FSM assignment](https://github.com/gameguild-gg/ai4games/) you already did:

```text
                 underpopulation (<2)          reproduction (==3)
   ┌─────────┐ ─────────────────────────►   ┌─────────┐
   │  ALIVE  │                              │  DEAD   │
   └─────────┘ ◄─────────────────────────   └─────────┘
                 overpopulation (>3)
        (no transition firing = survive: stay actions run)
```

| Conway rule                | Machine part                                   |
| -------------------------- | ---------------------------------------------- |
| underpopulation / overpop. | `Condition` on a transition leaving `Alive`    |
| reproduction               | `Condition` on a transition leaving `Dead`     |
| survival                   | implicit: stay actions run when nothing fires  |
| dies / is born             | `Action` writing `world.SetNext`               |
| one generation             | `Step` builds an `AgentContext` per cell and runs `StateMachine::Update` |

Your work, in `// begin solution` / `// end solution` regions:

1. `apps/life/fsm/StateMachine.h` — the machine core: `Update` scans the current state's transitions, fires the first whose `Condition::Test` is true (exit actions → transition actions → entry actions), otherwise runs stay actions.
2. `apps/life/rules/JohnConway.cpp` — the Conway-flavored `Condition` and `Action` classes, the graph wiring in the constructor, the per-cell update loop in `Step`, and `CountNeighbors`.
3. `apps/life/rules/HexagonGameOfLife.cpp` (interactive only) — the same shape on a hex grid: 6 neighbors, row-parity offsets, your rule of choice (classic hex plays B2/S34).

The framework is shared by every cell. Where the data lives — the part that trips everyone:

| Piece                    | Holds                                    | Lifetime            |
| ------------------------ | ---------------------------------------- | ------------------- |
| `World` bit              | where each cell **is** (its persistent state) | swapped by `SwapBuffers`, generation after generation |
| `State` (`Alive`/`Dead`) | what a cell **does** in that situation (actions + transitions) | shared by every cell, stores nothing per cell |
| `AgentContext`           | what one cell **sees** this update (position, `isAlive`, `aliveNeighbors`) | thrown away after each `Update` |
| `StateMachine::current`  | a **cursor** over the shared graph, synced from the world bit via `SetCurrent` | one update |

Conditions only read the current generation; actions only write the next one — that is what keeps the double buffering honest.

::: warning "Don't fight the machine"

You can pass the fixtures with a plain `if (neighbors == 3)` in `Step` — nobody will stop you. But the scaffolding is all here: conditions and actions are one-liners, the wiring is declarative, and the hexagonal variant reuses everything. Doing it machine-style is less code than bypassing it, and it is the style graded on the architecture points below.

:::

## Input

The first line of the input are three numbers, C, L and T, the number of columns, lines and turns, respectively. The next L lines are the initial state of the cells, where each line has C characters, either `.` for dead cells or `#` for alive cells.

```text
5 5 4
.#...
..#..
###..
.....
.....
```

When you run `life-tests`, the runner feeds inputs and reads outputs in exactly this format, straight from the fixture files in `apps/life/tests/`; you never type anything into stdin.

## Output

The output is the state of the cells after T turns, in the same format as the input (L lines of C characters).

```text
.....
..#..
...#.
.###.
.....
```

## Grading

10 points total:

- 5 Points – passed on the formal test cases;
- 3 Points – state machine architecture: conditions, actions and transitions used as intended;
- 2 Points – code quality and properly submitted in Canvas.

## References

- [Animated Example](https://playgameoflife.com/)
- [Conway's Game of Life Wiki](https://conwaylife.com/wiki/Conway%27s_Game_of_Life)
- [Wikipedia](https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life)
- Millington, *AI for Games*, ch. 5 — the Condition/Action/Transition state machine design
