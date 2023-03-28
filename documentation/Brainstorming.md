Some thoughts on game engine development
REFS:
https://ogldev.org/
https://github.com/fffaraz/awesome-cpp
https://www.codeintrinsic.com/a-guide-to-start-creating-a-game-engine/
https://www.gamedesigning.org/learn/make-a-game-engine/
https://medium.com/geekculture/how-to-make-your-own-game-engine-and-why-ddf0acbc5f3
https://github.com/YvesBoyadjian/Scenario4j

Basic Steps
Setup environment
Basic demo on SDL2 usage
Window Manager - SDL to manage it
Render context manager - Glue to OpenGL
Draw simple 3d polygons - Use OpenGL correctly
ImGUI glue for debug tools
Input Manager - Use SDL as common ground
Event / messaging system - Avoid polling data, use event based subscrition
Main loop - to be super seeded by the job system task manager
Asset manager (model file loaders, animation files, texture files, shader files, etc. or use existing libraries like assimp, stb which save a lot of trouble).
Scene manager and scenegraph data structure to manage the objects that exist in your game.
- In the scenegraph use the input system to move a physics object around (i.e. player, which could be just a simple axis aligned bounding box, AABB, in the physics world) by applying forces to it, and in rendering side draw the model at the position that the physics engine simulated the player box moved to.
Audio manager. Clip play

Rendering wrappers: cameras, basic object culling, or maybe more advanced like quadtree or octree,
Converting model data to VBOs and VAO and managing that data in video cards ram,
Loading and managing textures and shaders,
Control render ordering, blending, etc. to speed things up if the design is clever enough to allow that.
Write vertex shaders for animation skinning (associating model data to bone changes), shaders for lighting - and maybe go all in with deferred shading through use of multiple render targets (MRT) and isolate the rendering pipeline almost entirely from your scenegraph.

Next add a physics system (most of the time use a free and badass physics library like 'bullet', 'ode' etc.),
Do level loading (OP is importing BSP files which have lighting and shadowing already builtin in), combine that with physics.


Add a scripting system and engine bindings to it to enable quicker prototyping, integrate it into the engine.

Honestly there's so much more, i hardly scratched the surface, but if this is a 100-step process then OP is basically on step 1. Which is good for them because even if they achieve nothing they will lear a lot. But literally the hardest part of the effort goes into taking all these complex part and making them tick, flexibly and quickly.

For example the quake and early doom games had a command system (the same thing you see in console) that made the game tick. So subtle, but everything that happened was a string executed through that command system which had bindings to everything in the engine. Like a nervous system. It was/is deceivingly elegant.

We haven't even talked about sound, networking, particle systems, and mullion more things and anything that game designers and artists could do. There is a lot that goes into it.



parallax mapping: https://youtu.be/qHDiiyPEnC4

engine rendering hd models with low poly model using dynamic tessellation+vertex interpolation+mesh normal mapping (sorry it's in spanish, I recorded it to share with friends): https://youtu.be/MiU4LtcY5wU

lights and bones: https://youtu.be/Bx7TL1iD6dA

displacement mapping + tessellation + normal mapping + depth of field: https://youtu.be/WcqPSTDquBk


About colors:
- An interesting demo: an equivalent geogessr for colors