# Developer & Agent Guidelines for LazyRTUI

This document provides developer guidelines, engineering constraints, and architectural context for AI coding agents and human developers working on the `lazyrtui` repository.

> **Design Authority Notice**: All architectural decisions, component specifications, threading models, and feature implementations MUST strictly conform to [`DESIGN.md`](file:///home/u/source/open_source/lazyrtui/DESIGN.md).

---

## 1. Project Overview

`lazyrtui` is a C++17 Terminal User Interface (TUI) application for ROS 2 built on top of [FTXUI](https://github.com/ArthurSonzogni/FTXUI). It connects to the ROS 2 graph to display real-time information about nodes, topics, services, actions, TF transforms, rosbags, and interfaces.

- **Language Standard**: C++17
- **Build System**: CMake (>= 3.14) & `ament_cmake`
- **ROS 2 Distribution**: Compatible with ROS 2 Humble, Iron, Jazzy, Rolling, etc.
- **Design Authority**: [`DESIGN.md`](file:///home/u/source/open_source/lazyrtui/DESIGN.md)

---

## 2. Dependency Management & Submodule Rules

All third-party non-ROS C++ dependencies MUST be placed in `third_party/` as **Git submodules** with explicit version suffixes in their folder paths.

### Strict Rules for Dependencies:
1. **NO `FetchContent`**: Do NOT use `FetchContent_Declare` or `FetchContent_MakeAvailable` in `CMakeLists.txt`. All non-ROS dependencies are pre-cloned as git submodules.
2. **Versioned Submodule Paths**:
   - `third_party/FTXUI_v7.0.3` (Tag: `v7.0.3`)
   - `third_party/yaml-cpp_0.9.0` (Tag: `yaml-cpp-0.9.0`)
   - `third_party/nlohmann_json_v3.12.0` (Tag: `v3.12.0`)
3. **CMake Inclusion**: Use `add_subdirectory(third_party/<module_version>)` in `CMakeLists.txt`.

---

## 3. Building & Testing Instructions

### Environment Setup
Before building or running commands, source ROS 2:
```bash
source /opt/ros/$ROS_DISTRO/setup.bash
```

### Build Commands
To build using standalone CMake:
```bash
./build.sh
```
To build using ROS 2 `colcon`:
```bash
./build.sh --ros2
```
To run the built binary:
```bash
./run.sh         # runs cmake-built or colcon-built binary
./run.sh --ros2  # specifically sources install/setup.bash and runs colcon binary
```

### Running Unit Tests
To run unit tests via colcon:
```bash
colcon test --packages-select lazyrtui
colcon test-result --all --verbose
```

---

## 4. Code Structure & Architecture Guidelines

### Architecture Overview
- **TUI Layer (`LazyRTUIApp`)**:
  - Files: [`src/app.cpp`](file:///home/u/source/open_source/lazyrtui/src/app.cpp), [`include/lazyrtui/app.hpp`](file:///home/u/source/open_source/lazyrtui/include/lazyrtui/app.hpp)
  - Uses FTXUI components (`Renderer`, `Container`, `CatchEvent`, `ScreenInteractive`).
  - *Header inclusion rule*: Include `<ftxui/component/component.hpp>` and `<ftxui/component/screen_interactive.hpp>` directly in `app.hpp`. Avoid forward-declaring `ftxui::ScreenInteractive` as a `class` (in FTXUI v7+ it is a type alias `using ScreenInteractive = App`).
- **Data Double-Buffering Layer (`UiSnapshot` & `SnapshotStringList`)**:
  - Writers publish an immutable `UiSnapshot` under `data_mutex_`; FTXUI `Render()` lambdas read the published snapshot lock-free to guarantee 60 FPS rendering without blocking on graph queries or locks.
- **ROS 2 Layer (`ROS2Manager`)**:
  - Files: [`src/ros_manager.cpp`](file:///home/u/source/open_source/lazyrtui/src/ros_manager.cpp), [`include/lazyrtui/ros_manager.hpp`](file:///home/u/source/open_source/lazyrtui/include/lazyrtui/ros_manager.hpp)
  - PIMPL pattern (`ROS2Manager::Impl`). Manages single persistent ROS 2 node (`lazy_rtui_node`), graph querying, dynamic subscriptions via CDR deserialization, and native C++ service/action clients without spawning shell subshells.
- **Plugin & Converter Engine (`PythonPluginEngine` & `FTXUIConverter`)**:
  - Files: [`src/python_plugin_engine.cpp`](file:///home/u/source/open_source/lazyrtui/src/python_plugin_engine.cpp), [`src/ftxui_converter.cpp`](file:///home/u/source/open_source/lazyrtui/src/ftxui_converter.cpp)
  - Executes Python message render scripts under GIL protection (`PyGILState_Ensure`/`Release`), converts output JSON UI specs to FTXUI `Element`/`Canvas` widgets.
- **Config Loader (`ConfigLoader`)**:
  - Files: [`src/config_loader.cpp`](file:///home/u/source/open_source/lazyrtui/src/config_loader.cpp), [`include/lazyrtui/config_loader.hpp`](file:///home/u/source/open_source/lazyrtui/include/lazyrtui/config_loader.hpp)
  - Parses YAML configuration files using `yaml-cpp`.
- **TF Tree (`TFTree`)**:
  - Files: [`src/tf_tree.cpp`](file:///home/u/source/open_source/lazyrtui/src/tf_tree.cpp), [`include/lazyrtui/tf_tree.hpp`](file:///home/u/source/open_source/lazyrtui/include/lazyrtui/tf_tree.hpp)
  - Thread-safe coordinate frame hierarchy and transform calculations.

### Coding Guidelines
- All project code MUST reside inside the `lazyrtui` namespace (`namespace lazyrtui { ... }`).
- Keep UI rendering responsive and non-blocking. Never perform lock acquiring or ROS graph querying inside FTXUI `Render()` closures.
- Always use clickable `file://` links when pointing users to files in responses.

### Code Formatting (clang-format)
- Code formatting follows the **LLVM style** (`clang-format -style=LLVM`).
- Key parameters: 2-space indentation, 80-column limit, no tabs, braces attached (`BreakBeforeBraces: Attach`), pointer/reference alignment on the right (`int* p`), short functions inline only.
