# MoBaGEn

MoBaGEn - Module Based Game Engine - Educational tool to teach game programming concepts.

Join us: [![say thanks](https://img.shields.io/badge/Say%20Thanks-👍-1EAEDB.svg)](https://github.com/InfiniBrains/mobagen/stargazers) [![Discord](https://img.shields.io/discord/956922983727915078)](https://discord.gg/9CdJeQ2XKB)

Metrics: [![Codacy Badge](https://app.codacy.com/project/badge/Grade/1d95c33e061442e39d7e1a697b28cb10)](https://www.codacy.com/gh/InfiniBrains/mobagen/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=InfiniBrains/mobagen&amp;utm_campaign=Badge_Grade) [![codecov](https://codecov.io/gh/InfiniBrains/mobagen/branch/master/graph/badge.svg?token=RTRPZ2HBZN)](https://codecov.io/gh/InfiniBrains/mobagen) ![GitHub language count](https://img.shields.io/github/languages/count/InfiniBrains/mobagen) ![GitHub search hit counter](https://img.shields.io/github/search/InfiniBrains/mobagen/todo) ![Lines of code](https://img.shields.io/tokei/lines/github.com/InfiniBrains/mobagen) ![GitHub all releases](https://img.shields.io/github/downloads/InfiniBrains/mobagen/total) ![GitHub contributors](https://img.shields.io/github/contributors/InfiniBrains/mobagen)

CI: [![Windows](https://github.com/InfiniBrains/mobagen/actions/workflows/windows.yml/badge.svg)](https://github.com/InfiniBrains/mobagen/actions/workflows/windows.yml) [![Web](https://github.com/InfiniBrains/mobagen/actions/workflows/web.yml/badge.svg)](https://github.com/InfiniBrains/mobagen/actions/workflows/web.yml) [![Linux](https://github.com/InfiniBrains/mobagen/actions/workflows/linux.yml/badge.svg)](https://github.com/InfiniBrains/mobagen/actions/workflows/linux.yml) [![OSX](https://github.com/InfiniBrains/mobagen/actions/workflows/osx.yml/badge.svg)](https://github.com/InfiniBrains/mobagen/actions/workflows/osx.yml) [![Release](https://github.com/InfiniBrains/mobagen/actions/workflows/release.yml/badge.svg)](https://github.com/InfiniBrains/mobagen/actions/workflows/release.yml) [![pages](https://github.com/InfiniBrains/mobagen/actions/workflows/pages/pages-build-deployment/badge.svg)](https://github.com/InfiniBrains/mobagen/actions/workflows/pages/pages-build-deployment) 

# Philosophy

1. Webassembly is the future. The initial plan is to create a Game Editor and Engine to run on browsers or wrap it in apps such as electron, wasm3 or any other;
2. The engine core should be written in C++;
3. Scripts in JS, TS or WASM, or any language via specialized module;
4. The core should be as small as possible and the modules should be as swappable as possible;
5. Modules are the way to customize and extend the engine. We should create modules to do specific tasks and glue them all together. Ex.: rendering module, physics module, audio module, etc;
6. A build is simply the combination of: a engine player, packed with the modules used, plugins and the assets;
7. The engine should be able to run on any platform, but the editor should be web-first;
8. The company will receive funds from the community to pay the developers and the servers to host the editor. The core will be open source and free to use;
9. The company will offer subscription model to have access to version control and remote build;
10. The company will also offer a marketplace to sell assets, plugins and modules.

# Try online

Before checking it online, some demos are intended to be implemented by you. So most of the demos would look like empty;

- [Empty demos](https://infinibrains.github.io/mobagen/).
- [Flocking demo](https://infinibrains.com/data/flocking) for [this AI class](https://docs.google.com/presentation/d/1OBEY-tb_ubgoq6Mk9lEsCFaYLINni3oPwjH8iAXEQQM/edit?usp=sharing).

Want to showcase your work here? [Create a pull request](https://github.com/InfiniBrains/mobagen/pulls).

Do you like it?

# Motivation
- Act as a central point to teach basic game dev concepts;
- Build a regional community around game dev to bring attention to our people;
- Have full control of all the processes from code to the delivery;

## Follow us!
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

# Why CPM
- Best package manager to import and use third party libs from source code with low maintenance code.

# Architecture
- Please refer to [this doc.](documentation/Architecture.md)
- [C++ core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)

# Restrictions
- Don't recreate all game engine stuff, glue them all. "Life es too short to build a game engine from scratch";
- If a library do not easily compile on all supported platforms, we won't use it.
- Use consolidated libs and do not reinvent the wheel (only if is strictly needed)
- Start small and make small steps towards the goal.
- Always make everything lib interaction swappable as good as we can.
- Never use any lib on the core code. The core should interact with the libs through a glue code. Ex.: core lib -> physics module -> bullet physics engine
- Target the latest C++ version that is portable to our targets(iOS, Android, Windows, Linux, OSX, WEB). [compiler support table](https://en.cppreference.com/w/cpp/compiler_support).
- CI should be able to build to all targets at once.

# Essential Modules
- [ ] Core
- [ ] Job/Task Module
- [ ] Resources Manager

# Optional modules
- [ ] Graphics
  - [x] 2D - Needs to not rely on SDL_Renderer anymore
  - [ ] 3D
- [ ] Physics
- [ ] Sound
- [ ] Networking
- [ ] Scripting
- [ ] GUI
  - [x] ImGUI via 2D renderer - needs to become agnostic
- [ ] Input
- [ ] Game Components
- [ ] Scene
- [ ] Logger

# Todo List
- [ ] Image Editor Application
    - [ ] Design Editor similar to Slicer3D
    - [ ] DICOM loader
    - [ ] 3 viewports synced with exam data
- [ ] Game Editor Applications
    - [ ] Compilation of scripts
    - [ ] Execution of scripts within their respective contexts
    - [ ] Manipulation of variables within script contexts
    - [ ] Support importing and exporting compiled scripts

[![Star History Chart](https://api.star-history.com/svg?repos=InfiniBrains/mobagen&type=Date)](https://star-history.com/#InfiniBrains/mobagen&Date)

[![Anurag's GitHub stats](https://github-readme-stats.vercel.app/api?username=tolstenko)](https://github.com/anuraghazra/github-readme-stats)
