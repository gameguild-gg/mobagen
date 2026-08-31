# Flocking agents behavior assignments

<details>
<summary> Teacher Notes </summary>

Day 1:

- Flocking Presentation
- Talk about the formal vs interactive(freedom)
- Ensure EVERYONE have the codebase ready
- Explain where they should code in the formal and in the mobagen boilerplate;
- For the interactive assignments:
    - Always record videos (optionally upload to youtube as unlisted) and submit the link on the assignment.
    - If you are pursuing bonus extra points, state them as comment on the submission
    - If you want to do things by yourself or in a game engine, remember always create debug interfaces.
- For the formal assignment:
    - There is no need to nail all of the tests, but at least the basic ones should pass.

Day 2:

- Explain in depth common problems the students may have, such as:
    - Forgetting to normalize the force,
    - The separation force should be inverse proportional to the distance,
    - The alignment force should be proportional to the velocity,
    - When a force should include itself or not.
- Track the progress

</details>

You are in charge of implementing some functions to make some AI agents flock together in a game. After finishing it, you will be one step further to render it in a game engine, and start making reactive NPCs and enemies. You will learn the basic concepts needed to code and customize your own AI behaviors.

- [Presentation](https://docs.google.com/presentation/d/1OBEY-tb_ubgoq6Mk9lEsCFaYLINni3oPwjH8iAXEQQM/edit?usp=sharing)

Everything happens in **this one repo**. For the formal assignment you do not clone anything else:

- **Formal assignment:** the automated tests live here. The fixtures are `.in`/`.out` pairs in `apps/flocking/tests/`, replayed by the `flocking-tests` runner. Your implementation surface is the same rule classes the demo app uses: the `computeForce` bodies of `apps/flocking/behaviours/CohesionRule.cpp`, `AlignmentRule.cpp` and `SeparationRule.cpp`. There is no separate formal header to fill in.
- **Interactive assignment:** the `flocking` demo app in this repo renders your rules live. You can still code in any language and/or game engine you want, but working here is the shortest path. If you want extra experience, you may want to explore building bare minimum with C++, CMake and sdl: [SDL3-CPM-CMake-Example](https://github.com/gameguild-gg/SDL3-CPM-CMake-Example), or with raylib [raylib-cpm-cmake-boilerplate](https://github.com/gameguild-gg/raylib-cpm-cmake-boilerplate).

::: tip "Your edit and test loop"

From the repo root:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --parallel --target flocking-tests
./build/bin/flocking-tests
```

Edit the `computeForce` bodies, rebuild, rerun. That is the whole loop.

The report prints one line per fixture, `[fixture] <name> PASS` or `REJECT`, then a summary such as `Flocking formal tests: 18/20 passed (90.0%)`. The exit code stays nonzero until every fixture passes, and the failing ones are listed again under `Rejected:`. A CI workflow publishes the same report on every push that touches `apps/flocking/`.

:::

::: warning "Game Engines"

I don't recommend using Game Engines for this specific assignment. Historically, students fail on the implementation of the double buffering and the math operations. But if you are confident, go ahead.

:::

The [ai4games repo](https://github.com/gameguild-gg/ai4games/) is the original home of this assignment. Keep it as a historical reference only; everything you need is here.

::: note "Which rules apply where?"

The exact formulas and the console input/output format below are **required for the formal assignment** (this repo, checked by the `flocking-tests` runner). For the **interactive assignment** (this boilerplate or any engine) you have freedom: keep the three core behaviors and their intent, but constants, extra rules and how you integrate the forces are up to you.

:::

::: danger "Notes on imprecision"

On the formal assignment, the automated tests results may differ somehow because of floating point imprecision, so don't worry much. If you cannot make it pass 100% of the tests, explain how you tried to solve it and what you think is wrong. I will evaluate your code based on your explanation.

If you find an issue on my formal description or on the tests, send a PR and I will give you extra points.

:::

## What is flocking?

Flocking is a behavior that is observed in birds, fish and other animals that move in groups. It is a very simple behavior that can be implemented with a few lines of code. The idea is that each agent will try to move towards the center of mass of the group (cohesion), and will try to align its velocity with the average velocity of the group (AKA alignment). In addition, each agent will try to avoid collisions with other agents (AKA avoidance).

::: note "Formal Notation Review"

- $ \vec{F} $ means a vector $ F $ that has components. In a 2 dimensional vector it will hold $ F_x $ and $ F_y $. For example, if $ F_x = 1 $ and $ F_y = 3 $, then $ \vec{F} = (1,3) $
- Simple math operations between vectors are done component-wise. For example, if $ \vec{F} = (1,1) $ and $ \vec{G} = (2,2) $, then $ \vec{F} + \vec{G} = (3,3) $
- The notation $ \overrightarrow{P_1 * P_2} $ means the vector that goes from $ P_1 $ to $ P_2 $. It is the same as $ P_2-P_1 $
- The modulus notation means the length (magnitude) of the vector. $ |\vec{F}| = \sqrt{F_x^2+F_y^2} $ For example, if $ \vec{F} = (1,1) $, then $ |\vec{F}| = \sqrt{2} $
- The hat ^ notation means the normalized vector(magnitude is 1) of the vector. $ \hat{F} = \frac{\vec{F}}{|\vec{F}|} $ For example, if $ \vec{F} = (1,1) $, then $ \hat{F} = (\frac{1}{\sqrt{2}},\frac{1}{\sqrt{2}}) $
- The hat notation over 2 points means the normalized vector that goes from the first point to the second point. $ \widehat{P_1P_2} = \frac{\overrightarrow{P_1P_2}}{|\overrightarrow{P_1P_2}|} $ For example, if $ P_1 = (0,0) $ and $ P_2 = (1,1) $, then $ \widehat{P_1P_2} = (\frac{1}{\sqrt{2}},\frac{1}{\sqrt{2}}) $
- The sum $ \sum $ notation means the sum of all elements in the list going from `0` to `n-1`. Ex. $ \sum_{i=0}^{n-1} \vec{V_i} = \vec{V_0} + \vec{V_1} + \vec{V_2} + ... + \vec{V_{n-1}} $

:::

It is your job to implement those 3 behaviors following the ruleset below:

### Cohesion

Apply a force towards the center of mass of the group.

1. The $ n $ neighbors of an agent are all the other agents that are within a certain radius $ r_c $( `<=` operation, boundary-inclusive, verified against the fixtures ) of the agent. It doesn't include the agent itself;
2. Compute the location of the center of mass of the group ($ P_{CM} $);
3. Compute the force that moves the agent towards the center of mass ($ \overrightarrow{F_c} $): it is the direction from the agent to the center of mass, normalized.

![cohesion](https://i.imgur.com/sWs6IiN.png)

$$
P_{CM} = \frac{\sum_{i=0}^{n-1} P_i}{n}
$$

$$
\overrightarrow{F_{c}} = \begin{cases}
\widehat{P_{agent}P_{CM}} & \text{if } n > 0 \land |\overrightarrow{P_{agent}P_{CM}}| > 0 \\
\vec{0} & \text{otherwise}
\end{cases}
$$

::: tip

Note that the magnitude of $ \overrightarrow{F_c} $ is always at most 1. This value can be multiplied by a constant $ K_c $ to increase or decrease the cohesion force to look more appealing.

:::

In order for you to pass the formal assignment tests, normalize the force. The multiplication by the cohesion constant $ K_c $ happens in the weighted sum (see [Behavior composition](#behavior-composition)). No need to divide by the radius here.

[![cohesionflow](https://app.code2flow.com/LjRYWNnhilPO.code.png)](https://app.code2flow.com/LjRYWNnhilPO)

::: example "Cohesion Example"

![cohesion](https://i.imgur.com/NkhL5SJ.gif)

:::

### Alignment

It is the force that will align the velocity of the agent with the average velocity of the group.

1. The $ n $ neighbors of an agent are all the agents that are within the alignment radius $ r_a $ of the agent, including itself;
2. Compute the average velocity of the group ($ \overrightarrow{V_{avg}} $);
3. Compute the force that will move the agent towards the average velocity ($ \overrightarrow{F_{a}} $);

![alignment](https://i.imgur.com/YNMQ1hx.png)

$$
\overrightarrow{V_{avg}} = \frac{\sum_{i=0}^{n-1} \vec{V_i}}{n}
$$

$$
\overrightarrow{F_{a}} = \overrightarrow{V_{avg}}
$$

In order for you to pass the formal assignment tests, the alignment force is simply the average velocity. The multiplication by the alignment constant $ K_a $ happens in the weighted sum (see [Behavior composition](#behavior-composition)).

::: warning "Do not normalize"

Unlike cohesion and separation, do not normalize the alignment force. Its magnitude carries the speed of the group: the force is proportional to the velocity. Normalizing it would throw that information away.

:::

[![alignmentflow](https://app.code2flow.com/PyWDVmTaLS9W.code.png)](https://app.code2flow.com/PyWDVmTaLS9W)

::: danger

The alignment "force" is a weighted velocity normalization process, it is not a force. So the process of combining the velocities is not that precise here. In a real case, you should apply another process to improve look and feel. What we are doing here is a simplification acting as a force.

:::

::: example "Alignment Example"

![alignment](https://i.imgur.com/LutONL7.gif)

:::

### Separation

It will move the agent away from other agents when they get too close.

1. The $ n $ neighbors of an agent are all the other agents that are within the separation radius $ r_s $ of the agent;
2. If the distance to a neighbor is within the separation radius (inclusive), then the agent will move away from it inversely proportionally to the distance between them.
3. Accumulate the forces that will move the agent away from each neighbor ($ \overrightarrow{F_{s}} $). And then, clamp the force to a maximum value of $ F_{Smax} $.

![separation](https://i.imgur.com/M8tjJSp.png)

$$
\overrightarrow{F_s} = \sum_{i=0}^{n-1} \begin{cases}
\frac{\widehat{P_aP_i}}{|\overrightarrow{P_aP_i}|} & \text{if } \varepsilon < |\overrightarrow{P_aP_i}| \leq r_s \\
0 & \text{if } |\overrightarrow{P_aP_i}| \leq \varepsilon \lor |\overrightarrow{P_aP_i}| > r_s
\end{cases}
$$

::: note "Division-by-zero guard"

Never compare against exact zero. Guard the division with a small epsilon, and use exactly this constant: `distance > 0.0001f`. A neighbor closer than $ \varepsilon = 0.0001 $ is treated as coincident and contributes zero force. Without the guard, a neighbor at (or near) distance 0 produces a division by zero and the force blows up to infinity/NaN.

:::

::: tip

Here you can see that if we have more than one neighbor and one of them is way too close, the force will be very high and make the influence of the other neighbors irrelevant. This is the expected behavior.

:::

The epsilon guard above prevents the division by zero when the distance is exactly 0. But the force will still go near infinite when the distance is merely very small. To avoid this, after accumulating all the influences from every neighbor, the force will be clamped to a maximum magnitude of $ F_{Smax} $.

$$
\overrightarrow{F_{s}} = \begin{cases}
\overrightarrow{F_s} & \text{if } |\overrightarrow{F_s}| \leq F_{Smax} \\
\widehat{F_s} \cdot F_{Smax} & \text{if } |\overrightarrow{F_s}| > F_{Smax}
\end{cases}
$$

::: tip

- You can implement those two math together, but it is better to isolate in two steps to make it easier to understand and debug.
- This is not an averaged force like the cohesion force, it is a sum of forces. So, the maximum magnitude of the force can be higher than 1.

:::

In order for the formal tests to pass, you have to accumulate the forces $ \frac{\widehat{P_aP_i}}{|\overrightarrow{P_aP_i}|} $ for each neighbor (exactly as in the formula above) and then clamp the total to $ F_{Smax} $. The multiplication by the separation constant $ K_s $ happens in the weighted sum (see [Behavior composition](#behavior-composition)), not here.

[![separationFlow](https://app.code2flow.com/EkvGThGW36SH.code.png)](https://app.code2flow.com/EkvGThGW36SH)

::: example "Separation Example"

![separation](https://i.imgur.com/s78a8ru.gif)

:::

## Behavior composition

The force composition is made by a weighted sum of the influences of those 3 behaviors. This is the way we are going to work, this is not the only way to do it, nor the more correct. It is just a way to do it.

- $ \vec{F} = K_c \cdot \overrightarrow{F_c} + K_s \cdot \overrightarrow{F_s} + K_a \cdot \overrightarrow{F_a} $ `This is a weighted sum!`
- $ \overrightarrow{V_{new}} = \overrightarrow{V_{cur}} + \vec{F} \cdot \Delta t $ `This is a simplification!`
- $ P_{new} = P_{cur}+\overrightarrow{V_{new}} \cdot \Delta t $ `This is an approximation!`

::: warning

A more precise way for representing the new position would be to use full equations of motion. But given timestep is usually very small and it even squared, it is acceptable to ignore it. But here they are anyway, just dont use them in this assignment:

- $ \overrightarrow{V_{new}} = \overrightarrow{V_{cur}}+\frac{\overrightarrow{F}}{m} \cdot \Delta t $
- $ P_{new} = P_{cur}+\overrightarrow{V_{cur}} \cdot \Delta t + \frac{\vec{F}}{m} \cdot \frac{\Delta t^2}{2} $

Where:

- $ \overrightarrow{F} $ is the force applied to the agent;
- $ \overrightarrow{V} $ is the velocity of the agent;
- $ P $ is the position of the agent;
- $ m $ is the mass of the agent, here it is always 1;
- $ \Delta t $ is the time frame (1/FPS);
- $ cur $ is the current value of the variable;
- $ new $ is the new value of the variable to be used in the next frame.

The $ \overrightarrow{V_{new}} $ and $ P_{new} $ are the ones that will be used in the next frame and you will have to print to the console at the end of every single frame.

:::

[![combinedFlow](https://app.code2flow.com/hBCfg7YGeA4P.code.png)](https://app.code2flow.com/hBCfg7YGeA4P)

::: note

- For simplicity, we are going to assume that the mass of all agents is 1.
- In a real game simulation, it would be nice to apply some friction to the velocity of the agent to make it stop eventually or just clamp it to prevent the velocity get too high. But, for simplicity, we are going to ignore it.

:::

::: example "Combined behavior examples"

Alignment + Cohesion:

![alignment+cohesion](https://i.imgur.com/XX8T8Hi.gif)

Cohesion + Separation:

![separation+cohesion](https://i.imgur.com/xuSQDoT.gif)

Separation + Alignment:

![separation+alignment](https://i.imgur.com/wd8bCh6.gif)

All 3:

![alignment+cohesion+separation](https://i.imgur.com/8NAJ1mS.gif)

:::

## Input

The input consists in a list of parameters followed by a list of agents. The parameters are:

- $ r_c $ - Cohesion radius
- $ r_s $ - Separation radius
- $ F_{Smax} $ - Maximum separation force
- $ r_a $ - Alignment radius
- $ K_c $ - Cohesion constant
- $ K_s $ - Separation constant
- $ K_a $ - Alignment constant
- $ N $ - Number of agents

Every agent is represented by 4 values in the same line, separated by a space:

- $ x $ - X coordinate
- $ y $ - Y coordinate
- $ vx $ - X velocity
- $ vy $ - Y velocity

After reading the agent's data, the program should read the time frame ($ \Delta t $), simulate the agents and then output the new position of the agents in the same sequence and format it was read. The program should keep reading the time frame and simulating the agents until the end of the input.

When you run `flocking-tests`, the runner feeds inputs and reads outputs in exactly these formats, straight from the fixture files in `apps/flocking/tests/`; you never type anything into stdin.

::: note "Data Types"

All values are double precision floating point numbers to improve consistency between different languages.

:::

### Input Example

In this example we are going to test only the cohesion behavior. The input is composed by the parameters and 2 agents.

```text
1.000 0.000 0.000 0.000 1.000 0.000 0.000 2
0.000 0.500 0.000 0.000
0.000 -0.500 0.000 0.000
0.125
```

## Output

The expected output is the position and velocity for each agent after the simulation step using the time frame. After printing each simulation step, the program should wait for the next time frame and then simulate the next step. All values should have exactly 3 decimal places and should be rounded to the nearest.

```text
0.000 0.484 0.000 -0.125
0.000 -0.484 0.000 0.125
```

## Grading

10 points total:

- 3 Points – by following standards;
- 2 Points – properly submitted in Canvas;
- 5 Points – passed on test cases;