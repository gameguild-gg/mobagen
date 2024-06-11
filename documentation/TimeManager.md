# Game loop and time management

In a game engine we commonly have 2 types of time management for the game loop. One for fixed time step and another one for variable time step. Let's discuss the differences between them.

## Game loop with fixed time step

In the fixed time step, we run things that expect to happen in a constant rate, such as rendering that follows the monitor refresh rate, or network sync update running 5 times per second.

## Game loop with variable time step

In a variable time step, the game loop run as fast as the computer permits. This offers a smoothen experience to the user.

## Time manager

The time manager is responsible for triggering the different game loops and managing time for the game.