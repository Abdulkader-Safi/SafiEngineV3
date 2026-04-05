# SafiEngine

A pure-C game engine built on:

- **SDL3** + **SDL_gpu** — windowing, input, and a modern cross-platform GPU API (Metal / Vulkan / D3D12)
- **SDL_shadercross** — write HLSL once, get SPIR-V / MSL / DXIL at runtime
- **flecs** — Bevy-like archetype ECS with components, systems, queries, pipelines
- **cglm** — C SIMD math (vec / mat / quat)
- **cgltf + stb_image** — glTF 2.0 loading
- **Dear ImGui** (via **cimgui**) — debug overlay

Everything is pulled in via CMake `FetchContent`. You need **only** a C/C++ compiler and CMake ≥ 3.24 — no `brew install sdl3`, no shader toolchains, no submodules to sync.

## Build & run

```bash
# from the SafiEngine/ directory
cmake -S . -B build
cmake --build build -j

./build/examples/gltf_viewer/gltf_viewer
```

The first configure downloads SDL3, flecs, cimgui and friends, and fetches `BoxTextured.glb` into `examples/gltf_viewer/assets/models/`.

## Controls (gltf_viewer demo)

| Key | Action |
|---|---|
| ← / → | Yaw the model |
| ↑ / ↓ | Pitch the model |
| A / D | Roll the model |
| W / S | Dolly camera (zoom in / out) |

The ImGui overlay shows backend name, FPS, and a live transform inspector.

## IDE / LSP (Zed, VS Code, Neovim — anything clangd)

`compile_commands.json` is emitted by CMake and **symlinked into the project root** automatically by `cmake/ClangdSetup.cmake`. Open any file under `engine/` or `examples/` in Zed and clangd will resolve SDL3, flecs, cglm, and cimgui headers without any further configuration.

A `.clangd` file at the repo root tells clangd to treat engine headers as C11 (not C++) and to build the one C++ translation unit (the cimgui bridge) as C++17.

## Project layout

```
SafiEngine/
├── CMakeLists.txt             # top-level, FetchContent, warnings, clangd symlink
├── cmake/
│   ├── Dependencies.cmake     # SDL3, flecs, cimgui, cglm, cgltf, stb, SDL_shadercross
│   ├── ClangdSetup.cmake      # compile_commands.json symlink + .clangd writer
│   └── cimgui_bridge.cpp      # C++ bridge exposing ImGui SDL3+SDL_gpu backends as C
├── engine/
│   ├── include/safi/          # public C headers (core, ecs, render, input, ui)
│   └── src/                   # implementation
└── examples/
    └── gltf_viewer/           # first demo: load a .glb, rotate with arrows + WASD
```

## Adding your own systems

```c
#include <safi/safi.h>

static void move_system(ecs_iter_t *it) {
    SafiTransform *t = ecs_field(it, SafiTransform, 0);
    for (int i = 0; i < it->count; ++i) {
        t[i].position[1] += 1.0f * it->delta_time;
    }
}

int main(void) {
    SafiApp app;
    safi_app_init(&app, &(SafiAppDesc){ .title="My Game", .width=1280, .height=720 });

    ecs_world_t *world = safi_app_world(&app);
    ECS_SYSTEM(world, move_system, EcsOnUpdate, SafiTransform);

    safi_app_run(&app);
    safi_app_shutdown(&app);
}
```

See [`Docs/docs/api/`](../Docs/docs/api) for the full API reference (one MDX page per subsystem).

## Status

Early days. The first milestone (glTF + spin with keyboard controls) is the current target. Out of scope for this pass: PBR, shadows, skeletal animation, audio, asset hot-reload, scene serialization.
