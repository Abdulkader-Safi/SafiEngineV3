# SafiEngine

A pure-C game engine built on:

- **SDL3** + **SDL_gpu** — windowing, input, and a modern explicit GPU API (Metal, Vulkan, D3D12). Shaders are authored once in HLSL and compiled at build time to SPIR-V + MSL via glslang + spirv-cross.
- **flecs** — Bevy-like archetype ECS with components, systems, queries, pipelines
- **cglm** — C SIMD math (vec / mat / quat)
- **cgltf + stb_image** — glTF 2.0 loading
- **[MicroUI](https://github.com/rxi/microui)** — tiny pure-C immediate-mode UI for the debug overlay. The engine ships its own SDL_gpu batched-quad backend so nothing in this project is C++.

Everything is pulled in via CMake `FetchContent`. You need **only** a C/C++ compiler and CMake ≥ 3.24 — no `brew install sdl3`, no submodules to sync.

## Build & run

```bash
# from the SafiEngine/ directory
cmake -S . -B build
cmake --build build -j

./build/examples/gltf_viewer/gltf_viewer
```

The first configure downloads SDL3, flecs, MicroUI, cglm, cgltf, stb, glslang, and SPIRV-Cross, then fetches `BoxTextured.glb` into `examples/gltf_viewer/assets/models/`.

## Controls (gltf_viewer demo)

| Key   | Action                       |
| ----- | ---------------------------- |
| ← / → | Yaw the model                |
| ↑ / ↓ | Pitch the model              |
| A / D | Roll the model               |
| W / S | Dolly camera (zoom in / out) |

The MicroUI overlay shows a Scene hierarchy and a live Inspector with editable transform, camera, and light properties.

## IDE / LSP (Zed, VS Code, Neovim — anything clangd)

`compile_commands.json` is emitted by CMake and **symlinked into the project root** automatically by `cmake/ClangdSetup.cmake`. Open any file under `engine/` or `examples/` in Zed and clangd will resolve SDL3, flecs, cglm, and microui headers without any further configuration.

A `.clangd` file at the repo root tells clangd the project is C11. There are no C++ translation units under `engine/` — the library is pure C.

## Project layout

```
SafiEngine/
├── CMakeLists.txt             # top-level, FetchContent, warnings, clangd symlink
├── cmake/
│   ├── Dependencies.cmake     # SDL3, flecs, cglm, cgltf, stb, MicroUI, glslang, SPIRV-Cross
│   ├── SafiShaders.cmake      # safi_compile_shader() — HLSL → SPIR-V + MSL build helper
│   └── ClangdSetup.cmake      # compile_commands.json symlink + .clangd writer
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
