# LazyRTUI

**LazyRTUI** is a modern Terminal User Interface (TUI) for ROS 2, inspired by tools like `lazygit` and `k9s`. Built with C++17 and [FTXUI](https://github.com/ArthurSonzogni/FTXUI), it provides a fast, lightweight, and interactive terminal dashboard to monitor and interact with ROS 2 nodes, topics, services, actions, TF trees, and rosbags.

---

## Features

- 🖥️ **Nodes Management**: Browse active ROS 2 nodes and inspect their published/subscribed topics, services, and action clients.
- 📡 **Topics Inspector**: List active topics, view message types, monitor publication rate, and echo live topic messages.
- ⚙️ **Services & Actions**: List available services and actions, inspect request/response schemas, and invoke them with JSON payloads.
- 🌲 **TF Tree Visualizer**: Inspect coordinate frames, view frame parent-child hierarchies, and check translation/rotation quaternions.
- 📦 **Rosbag Helper**: Quick commands and status overview for recording and playing ROS 2 bags.
- 📋 **Interfaces Explorer**: Browse ROS 2 message, service, and action definitions.
- ⌨️ **Vim-style Keybindings & Help Modal**: Quick navigation with `1-8`, `j/k`, `w`, `r`, `e`, `c`, `g`, `?`, and `q`.

---

## Dependencies & Requirements

- **ROS 2** (Humble, Iron, Jazzy, Rolling, or compatible distribution)
- **C++17 Compiler** (GCC >= 9.0 or Clang >= 10.0)
- **CMake** (>= 3.14)

### Third-Party Submodules (Included in `third_party/`)

The repository uses Git submodules for core C++ dependencies, located under `third_party/`:
- [FTXUI v7.0.3](file:///home/u/code/open_source/lazyrtui/third_party/FTXUI_v7.0.3) — Terminal UI framework
- [yaml-cpp 0.9.0](file:///home/u/code/open_source/lazyrtui/third_party/yaml-cpp_0.9.0) — YAML parser and emitter
- [nlohmann_json v3.12.0](file:///home/u/code/open_source/lazyrtui/third_party/nlohmann_json_v3.12.0) — JSON library for C++

---

## Getting Started

### 1. Clone the Repository

Clone with submodules initialized:
```bash
git clone --recursive https://github.com/your-username/lazyrtui.git
cd lazyrtui
```

If you already cloned without submodules:
```bash
git submodule update --init --recursive
```

### 2. Build

Ensure your ROS 2 environment is sourced:
```bash
source /opt/ros/<ros_distro>/setup.bash
```

- **Standalone CMake build** (default):
  ```bash
  ./build.sh
  ```
- **ROS 2 `colcon` build**:
  ```bash
  ./build.sh --ros2
  ```
*(Note: `./build.sh` automatically cleans previous build artifacts when switching between CMake and colcon modes to prevent conflicts.)*

### 3. Run

Launch the compiled binary:
```bash
./run.sh
```
Or launch specifically with `--ros2`:
```bash
./run.sh --ros2
```

---

## Keybindings

| Key | Action |
| --- | --- |
| `1` - `8` | Switch Tabs (Nodes, Topics, Services, Actions, Interfaces, Bags, TF, About) |
| `w` | Toggle pane focus (Left / Right) |
| `j` / `k` | Navigate lists (Vim style) |
| `r` | Refresh ROS 2 graph data |
| `e` | Toggle topic echo (Topics tab) |
| `c` | Call selected service (Services tab) |
| `g` | Send goal to selected action (Actions tab) |
| `?` | Toggle help modal |
| `q` | Quit LazyRTUI |

---

## Project Structure

```
lazyrtui/
├── CMakeLists.txt              # Build configuration using third_party submodules
├── package.xml                 # ROS 2 package manifest
├── build.sh                    # Build script
├── run.sh                      # Launcher script
├── config/                     # Configuration files (UI settings, refresh rates)
├── include/lazyrtui/           # Header files
│   ├── app.hpp                 # LazyRTUIApp TUI class
│   ├── ros_manager.hpp         # ROS 2 node & client manager
│   ├── config_loader.hpp       # Config parser
│   └── tf_tree.hpp             # TF tree data structures
├── src/                        # Source implementation
│   ├── main.cpp                # Main entrypoint
│   ├── app.cpp                 # FTXUI layout and event handling
│   ├── ros_manager.cpp         # ROS 2 API integration
│   ├── config_loader.cpp       # Config loader implementation
│   └── tf_tree.cpp             # TF tree calculations
└── third_party/                # Git submodules with version tags
    ├── FTXUI_v7.0.3/
    ├── yaml-cpp_0.9.0/
    └── nlohmann_json_v3.12.0/
```

---

## License

MIT License.
