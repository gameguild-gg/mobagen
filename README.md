# MoBaGEn

[![Windows](/actions/workflows/windows.yml/badge.svg)](/actions/workflows/windows.yml) [![Web](/actions/workflows/web.yml/badge.svg)](/actions/workflows/web.yml) [![Linux](/actions/workflows/linux.yml/badge.svg)](/actions/workflows/linux.yml) [![OSX](/actions/workflows/osx.yml/badge.svg)](/actions/workflows/osx.yml) [![pages-build-deployment](/actions/workflows/pages/pages-build-deployment/badge.svg)](/actions/workflows/pages/pages-build-deployment)

# Motivation
I started this game engine project because:
- I want to sharpen my C++ skills; 
- I love game engine architecture;
- I don't want to recreate all game engine stuff, I want to glue them all. "Life es too short to build a game engine from scratch"  
- I want to have full control of all the processes from code to the delivery;
- I want a job as a Game Engine Developer.

## Hire Me!
Did you like this project? Follow me on [social medias](https://linktree.com/tolstenko)

# Why C++
- Has many solid, well maintained, efficient and stable libs;
- It speaks straight to hardware, you can do whatever you want;
- It is portable.

# Why CMAKE
It is the best all-around solution to:
- Create projects cross-platform
- Configure variables and flags
- Build complex projects in easy way
- Most relevant libs supports it 

# Why CPP-PM
- Best package manager to import and use third party libs from source code with low maintenance code.

# Architecture
Please refer to [this doc.](docs/Architecture.md)

# Restrictions
- If a library do not easily compile on all supported platforms, we won't use it.
- Use consolidated libs and do not reinvent the wheel (only if is strictly needed)
- Start small and make small steps towards the goal.
- Always make everything lib interaction swappable as good as we can.
- Never use any lib on the core code. The core should interact with the libs through a glue code. Ex.: core lib -> physics module -> bullet physics engine
- Target the latest C++ version that is portable to our targets(iOS, Android, Windows, Linux, OSX, WEB). [compiler support table](https://en.cppreference.com/w/cpp/compiler_support).
- CI should be able to build to all targets at once.

# Examples
If you want to try code some AI examples, you need to follow this guide in order undestand and do your activity. https://docs.google.com/presentation/d/1OBEY-tb_ubgoq6Mk9lEsCFaYLINni3oPwjH8iAXEQQM/edit?usp=sharing
- [Flocking demo](https://infinibrains.com/data/flocking)