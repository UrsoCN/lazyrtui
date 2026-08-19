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

To run unit tests with the standalone CMake build:
```bash
source /opt/ros/$ROS_DISTRO/setup.bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

To run a single test target: `./build/<target>` (e.g. `./build/test_python_plugin_engine`).

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

---

## 5. Testing Paradigm (Test-First)

### Principle
Development follows a **test-first** discipline: every functional change (feature, bug fix, refactor) is accompanied by unit tests that pin the new behavior. Tests are written against the behavior spec before or together with the implementation, and a fix is only complete when the test that reproduces the original defect passes. This is not optional for the orchestrator or delegated agents.

### Framework & Targets
- Framework: **GoogleTest** via `ament_add_gtest` (ROS 2 standard). No other test framework is used.
- One test target per module under `test/`, wired in `CMakeLists.txt` under `if(BUILD_TESTING)`:

| Target | Module under test |
|---|---|
| `test_tf_tree` | `src/tf_tree.cpp` — frame tree semantics |
| `test_config_loader` | `src/config_loader.cpp` — YAML parsing & fallback chain |
| `test_ftxui_converter` | `src/ftxui_converter.cpp` — JSON UI spec → FTXUI rendering |
| `test_cdr_utils` | `src/cdr_utils.hpp` — CDR byte-swap/endianness |
| `test_python_plugin_engine` | `src/python_plugin_engine.cpp` — Python plugin engine (embeds CPython) |
| `test_app` | `src/app.cpp` — UI event handling, input focus & hotkey suppression |

New modules MUST get a matching test target in the same commit as the feature.

CMake wiring pattern (use `test_config_loader` as the template):
```cmake
ament_add_gtest(test_<module> test/<module>_test.cpp src/<module>.cpp)
target_include_directories(test_<module> PRIVATE include)
target_link_libraries(test_<module> <module-specific-deps>)
```
Module-specific deps: `yaml-cpp` (config), `ftxui::dom ftxui::screen` + `nlohmann_json::nlohmann_json` (converter), `${Python3_LIBRARIES}` + `nlohmann_json::nlohmann_json` (plugin engine, plus `${Python3_INCLUDE_DIRS}` in include dirs), `src` include dir for header-only modules (`test_cdr_utils`).

### Test-First Workflow
1. **Write the failing test first** — encode the required behavior as assertions (not crash-freedom; assert real output/semantics).
2. **Run it against current code** to confirm it fails for the right reason.
3. **Implement** the change, then make the test pass.
4. For pure-logic modules prefer **rendered-output or value assertions** over "doesn't throw": e.g. `ftxui::Screen` + `ToString()`/`CellAt` for FTXUI elements, JSON round-trip for the plugin engine.
5. Every test must be **deterministic and hermetic**: no dependency on host env, `HOME`, `CWD`, or network; use temp-dir fixtures that are cleaned up in `TearDown`.

### Hermeticity & Isolation Rules (learned from real failures)
- **Temp dirs must be unique per test** (pid + counter suffix). CPython caches directory listings in `sys.path_importer_cache` keyed by path string; deleting/recreating a shared path between tests serves stale entries and causes intermittent `No module named` failures. `std::filesystem::temp_directory_path()` is the base.
- **Python module names must be unique per test** — `sys.modules` caches imported modules for the whole process; a repeated name reuses a stale module.
- **Environment variables**: if a test sets `HOME`/etc., save the old value in `SetUp` and restore in `TearDown` (restore must run even on assertion failure — put it in `TearDown`, not inline after the call).
- **CWD**: tests that depend on relative paths must `chdir` into their fixture dir in `SetUp` and restore in `TearDown`, or use absolute paths — never assume the binary runs from the repo root.

### GIL / Refcount Discipline for Python-Embedding Tests
- The engine acquires the GIL internally via `GilGuard`; tests call only the public API.
- When a test exercises a fallback/exception path, a later Python import may crash if the engine leaks or double-releases refs — e.g. a double-DECREF in the invalid-JSON fallback freed the stdlib `json` module while `sys.modules["json"]` still referenced it, and the next `PyImport_ImportModule("json")` dereferenced freed memory. A segfault that appears only in full-suite runs (not in isolation) is almost always a refcount bug in `python_plugin_engine.cpp` — run the suite under gdb (`gdb -batch -ex run -ex bt ./build/<target>`) to confirm.

### Verification
- Before committing any change: build with `cmake --build build -j"$(nproc)"` and run `ctest --test-dir build --output-on-failure` (all targets must pass).
- Run the full suite, not just the changed target, when the change could affect shared code — e.g. `cdr_utils.hpp` is included by `src/ros_manager.cpp` (the main app) as well as `test_cdr_utils`, and `python_plugin_engine.cpp` exercises CPython state that persists across tests. The suite is 5 cheap targets (<1s); running all of it after any change costs little and catches cross-target interference.

