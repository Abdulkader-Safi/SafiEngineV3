---
pageType: home

hero:
  name: Safi Engine
  text: A pure-C game engine
  tagline: SDL3 GPU · Bevy-style ECS · Dear ImGui · zero-setup CMake
  actions:
    - theme: brand
      text: API Reference
      link: /api/
    - theme: alt
      text: GitHub
      link: https://github.com/safi/SafiEngine
  image:
    src: /rspress-icon.png
    alt: SafiEngine
features:
  - title: Pure C11, zero dependencies to install
    details: Just CMake and a C/C++ compiler. FetchContent pulls SDL3, flecs, cglm, cgltf, stb, cimgui, and SDL_shadercross on first configure. No brew, no apt, no submodules.
    icon: 🧊
    link: /api/
  - title: SDL_gpu cross-platform renderer
    details: Modern explicit GPU API built on Metal, Vulkan, and D3D12. Write HLSL once and SDL_shadercross translates to MSL, SPIR-V, or DXIL at runtime.
    icon: 🎮
    link: /api/render/overview
  - title: Bevy-style ECS in C
    details: Powered by flecs. Components, queries, systems, pipelines, observers, and singletons — with a familiar Bevy-inspired API, just in plain C.
    icon: 🧬
    link: /api/ecs/overview
  - title: Dear ImGui debug overlay
    details: cimgui bindings with the SDL3 and SDL_gpu backends wired up out of the box. Drop inspectors and tooling into any scene with one call.
    icon: 🪟
    link: /api/ui/debug_ui
  - title: glTF 2.0 loader
    details: Load .gltf and .glb straight into a GPU mesh and material via cgltf and stb_image. The starter demo rotates a textured box with the arrow keys and WASD.
    icon: 📦
    link: /api/render/gltf_loader
  - title: Zed / clangd friendly
    details: CMake emits compile_commands.json and symlinks it to the project root. Open any file in Zed, VS Code, or Neovim — clangd resolves every engine header with no extra config.
    icon: ⚡
    link: /api/
---
