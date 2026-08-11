# Developer & Agent Guidelines for LazyRTUI

This document provides developer guidelines and context for AI coding agents working on the `lazyrtui` repository.

---

## 1. Project Overview

`lazyrtui` is a C++17 Terminal User Interface (TUI) application for ROS 2 built on top of [FTXUI](https://github.com/ArthurSonzogni/FTXUI). It connects to the ROS 2 graph to display real-time information about nodes, topics, services, actions, TF transforms, and bags.

- **Language Standard**: C++17
- **Build System**: CMake (>= 3.14) & `ament_cmake`
- **ROS 2 Distribution**: Compatible with ROS 2 Jazzy, Humble, Rolling, etc.

---

## 2. Dependency Management & Submodules Rules

All third-party non-ROS C++ dependencies MUST be placed in `third_party/` as **Git submodules** with explicit version suffixes in their folder paths.

### Rules for Dependencies:
1. **NO `FetchContent`**: Do NOT use `FetchContent_Declare` or `FetchContent_MakeAvailable` in `CMakeLists.txt`. All dependencies are pre-cloned as git submodules.
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

*Note*: If `catkin_pkg` or ROS 2 Python scripts fail during CMake configuration because a user-local Python environment overrides system Python, explicitly pass `-DPython3_EXECUTABLE=/usr/bin/python3` to CMake.

---

## 4. Code Structure & Best Practices

### Architecture
- **TUI Layer (`LazyRTUIApp`)**:
  - Code: `src/app.cpp`, `include/lazyrtui/app.hpp`
  - Uses FTXUI components (`Renderer`, `Container`, `CatchEvent`, `ScreenInteractive`).
  - *Header inclusion rule*: Include `<ftxui/component/component.hpp>` and `<ftxui/component/screen_interactive.hpp>` directly in `app.hpp`. Avoid forward-declaring `ftxui::ScreenInteractive` as a `class`, because in FTXUI v7+ it is a type alias (`using ScreenInteractive = App`).
- **ROS 2 Layer (`ROS2Manager`)**:
  - Code: `src/ros_manager.cpp`, `include/lazyrtui/ros_manager.hpp`
  - Handles `rclcpp` node lifecycle, graph querying, message subscription, and ROS 2 service/action clients.
- **Config Loader (`ConfigLoader`)**:
  - Code: `src/config_loader.cpp`, `include/lazyrtui/config_loader.hpp`
  - Parses configuration files using `yaml-cpp`.
- **TF Tree (`TFTree`)**:
  - Code: `src/tf_tree.cpp`, `include/lazyrtui/tf_tree.hpp`
  - Manages coordinate frame hierarchy and transform calculation.

### Coding Guidelines
- All project code should reside inside the `lazyrtui` namespace (`namespace lazyrtui { ... }`).
- Keep UI rendering responsive and non-blocking. Thread-synchronize data shared between the ROS 2 executor thread and the FTXUI main event loop using `std::mutex`.
- Use clickable `file://` links when pointing users to files in responses.
