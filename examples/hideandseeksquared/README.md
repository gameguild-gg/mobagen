# Hide And Seek Demo

This AI demo consists in creating a game where a player should flee and hide from the AI agent.

## Goals

Main Goal:

- Learn how to implement a Visibility Algorithm or Visibility Graph applied on a Grid2D Data Structure;

Secondary Goals:

- Learn how to use Grid Data Structures;
- Learn how to use A* algorithm;
- Learn how to create agents to pursue the player;

References:

- https://www.albertford.com/shadowcasting/ - main reference on shadowcasting. This is the easiest way to implement a Visibility Algorithm.
- https://www.redblobgames.com/articles/visibility/ - if you intend to use raycasting and polygon shapes, this is a good reference
- https://www.redblobgames.com/pathfinding/a-star/introduction.html - To remember how A* works
- https://www.youtube.com/watch?v=fc3nnG2CG8U a very nice video explaining and showing how to implement a Visibility Algorithm

## Rules
 
- The more time the player stays hidden, the more points he gets; 
- The player can use the walls to hide; 
- The agent should pursue the player via shortest path; If the player hides, then the agent should go to the last known position of the player; And then resume roaming.
- Both the player and the agent can only see what ever his field of view permits. If a player can see the AI, the AI can see it too;
- You can use this demo here to do all code by yourself or you can use any game engine you prefer.

## What to code

It is expected you code the following:
- Change the variable visible of the node if the specific node is visible to the player or not.

Extras:

- If enemy is in the player's field of view, then the enemy should chase the player to the last known position of the player, any heuristic based path finding will work, implementing full A* is optional, because the path is visible;
- If the enemy don't see the player and don't have a last known position, then the enemy should roam around the map;


## Kahoot 

https://create.kahoot.it/details/1fe41440-aea9-49c1-a464-c2f38e662e57

a) Goals for the class to be observed
- Ability to teach complex data structures and algorithms;
- Ability to incentive the students to learn by themselves;

b) Description of the planned teaching/learning activities
- Revisit some of previous classes, Data Structures, Path Finding, MinMax, Monte Carlo Tree Search;
- Present the most common visibility algorithm in 2d grids such as Shadow Casting; 

c) Description of what students have been asked to do to prepare for this class
- Clone the repository;

d) Indication if the planned class is typical of your teaching style, and if not why.
- This class have some changes in style because it includes kahoot;
- Usually all classes are project and code based.