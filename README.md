# SafiEngineV3

A pure-C game engine and its documentation site, kept in one monorepo.

```
SafiEngineV3/
├── SafiEngine/   # the engine — C11, CMake, SDL3 GPU, flecs, Nuklear
└── Docs/         # the documentation site — rspress + MDX
```

## What is SafiEngine?

SafiEngine is a **pure-C** game engine with **zero manual dependency setup**. All you need is CMake and a C/C++ compiler — everything else is pulled in by `FetchContent` on the first configure.

| Concern        | Library                                                                                                 |
| -------------- | ------------------------------------------------------------------------------------------------------- |
| Window + input | [SDL3](https://github.com/libsdl-org/SDL)                                                               |
| GPU            | SDL_gpu — Metal, Vulkan, D3D12; shaders authored in HLSL, compiled to SPIR-V + MSL at build time        |
| ECS            | [flecs](https://github.com/SanderMertens/flecs) — Bevy-style in plain C                                 |
| Math           | [cglm](https://github.com/recp/cglm)                                                                    |
| glTF 2.0       | [cgltf](https://github.com/jkuhlmann/cgltf) + [stb_image](https://github.com/nothings/stb)              |
| Debug UI       | [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) — single-header, pure C, custom SDL_gpu backend |

The first milestone is a glTF viewer that loads `BoxTextured.glb` and lets you rotate it with the arrow keys and WASD while a Nuklear overlay shows a live transform inspector.

## Build the engine

```bash
cd SafiEngine
cmake -S . -B build
cmake --build build -j

./build/examples/gltf_viewer/gltf_viewer
```

The first configure downloads all third-party sources into `build/_deps/` and fetches the sample glTF asset. Subsequent builds are incremental.

### Controls

| Key   | Action                         |
| ----- | ------------------------------ |
| ← / → | Yaw the model                  |
| ↑ / ↓ | Pitch the model                |
| A / D | Roll the model                 |
| W / S | Dolly the camera (zoom in/out) |

## Run the docs site

```bash
cd Docs
bun install      # or npm install / pnpm install
bun run dev
```

The full API reference lives under `Docs/docs/api/` as one MDX page per public header — grouped into **core**, **ecs**, **render**, **input**, and **ui**. It renders with rspress directives (`:::tip`, `:::warning`), code blocks, and tabs.

## IDE / LSP

`SafiEngine/cmake/ClangdSetup.cmake` emits `compile_commands.json` and symlinks it into `SafiEngine/` after each configure. Any clangd-aware editor — **Zed**, VS Code, Neovim, CLion — picks it up automatically with no per-editor configuration.

A `SafiEngine/.clangd` file tells clangd the project is C11. The engine library is 100% C — there are no C++ translation units to configure.

## Repository layout

```
SafiEngineV3/
├── SafiEngine/
│   ├── CMakeLists.txt
│   ├── cmake/                       # Dependencies (FetchContent) + clangd setup
│   ├── engine/
│   │   ├── include/safi/            # public C headers (core, ecs, render, input, ui)
│   │   └── src/                     # implementation
│   ├── examples/
│   │   └── gltf_viewer/             # first demo
│   ├── .clangd
│   └── README.md
├── Docs/
│   ├── docs/
│   │   ├── index.md                 # rspress home
│   │   └── api/                     # ~18 MDX pages, one per engine API
│   ├── rspress.config.ts
│   └── package.json
├── .gitignore
└── README.md                        # ← you are here
```

## Status

Early-stage. The first pass is focused on getting a glTF model on screen and making the engine pleasant to hack on (fast builds, working LSP, clean docs). Explicit non-goals for now: PBR, shadows, skeletal animation, audio, hot-reload, scene serialization.

## License

TBD.
