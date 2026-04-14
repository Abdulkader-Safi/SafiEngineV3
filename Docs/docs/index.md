---
pageType: home

hero:
  name: Safi Engine
  text: A pure-C game engine
  tagline: SDL3 GPU · Bevy-style ECS · MicroUI · zero-setup CMake
  actions:
    - theme: brand
      text: API Reference
      link: /api/
    - theme: alt
      text: Roadmap
      link: /roadmap/
    - theme: alt
      text: GitHub
      link: https://github.com/safi/SafiEngine
  image:
    src: /rspress-icon.png
    alt: SafiEngine
features:
  - title: Pure C11, zero dependencies to install
    details: Just CMake and a C/C++ compiler. FetchContent pulls SDL3, flecs, cglm, cgltf, stb, and MicroUI on first configure. No brew, no apt, no submodules, no C++ in the engine.
    icon: 🧊
    link: /api/
  - title: SDL_gpu renderer
    details: Modern explicit GPU API with Metal, Vulkan, and D3D12 backends. Current milestone targets Metal on macOS with shaders authored in MSL; cross-platform shader pipelines are a planned follow-up.
    icon: 🎮
    link: /api/render/overview
  - title: Bevy-style ECS in C
    details: Powered by flecs. Components, queries, systems, pipelines, observers, and singletons — with a familiar Bevy-inspired API, just in plain C.
    icon: 🧬
    link: /api/ecs/overview
  - title: MicroUI debug overlay
    details: Tiny pure-C immediate-mode UI (~1100 SLOC) with a custom SDL_gpu batched-quad backend. Drop inspectors, number fields, and tooling into any scene without dragging C++ into a C engine.
    icon: 🪟
    link: /api/ui/debug_ui
  - title: glTF 2.0 loader
    details: Load .gltf and .glb straight into a GPU mesh and material via cgltf and stb_image. The starter demo rotates a textured box with the arrow keys and WASD.
    icon: 📦
    link: /api/render/gltf_loader
  - title: Audio with miniaudio
    details: 2D and 3D positional audio, mixer buses (master/music/sfx/ui), streaming for long files, and built-in WAV/FLAC/MP3 decoding. One handle-based C API, zero extra setup.
    icon: 🔊
    link: /api/audio/audio
  - title: Zed / clangd friendly
    details: CMake emits compile_commands.json and symlinks it to the project root. Open any file in Zed, VS Code, or Neovim — clangd resolves every engine header with no extra config.
    icon: ⚡
    link: /api/
---
