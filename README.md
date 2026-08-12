# LazyRTUI

**LazyRTUI** is a modern Terminal User Interface (TUI) for ROS 2, inspired by tools like `lazygit` and `k9s`. Built with C++17 and [FTXUI](https://github.com/ArthurSonzogni/FTXUI), it provides a fast, lightweight, and interactive terminal dashboard to monitor and interact with ROS 2 nodes, topics, services, actions, TF trees, rosbags, and interfaces.

> 📖 **Design Authority**: Detailed software architecture, threading model, and component specifications are documented in [`DESIGN.md`](file:///home/u/source/open_source/lazyrtui/DESIGN.md).

---

## Key Features

- 🖥️ **Nodes Management**: Browse active ROS 2 nodes and inspect published/subscribed topics and service servers.
- 📡 **Topics Inspector**: List active topics, view message types, monitor publication rates, and perform dynamic topic echoing.
- ⚙️ **Services & Actions**: List available services and actions, inspect request/response schemas, and invoke them with JSON payloads.
- 🎨 **IDL-Driven Python Plugins & ASCII Plotting**: Process ROS 2 message IDLs via Python plugins and render real-time terminal charts and widgets via `FTXUIConverter`.
- ⚡ **60 FPS Lock-Free Rendering**: Uses immutable snapshot double-buffering (`UiSnapshot`) so UI rendering never blocks on ROS graph queries or data locks.
- 🛡️ **Zero `~/.ros/log` Spam**: Maintains a single persistent ROS 2 node (`lazy_rtui_node`) and native C++ clients without spawning temporary shell subshells.
- 🌲 **TF Tree Visualizer**: Inspect coordinate frames, view frame parent-child hierarchies, and check translation/rotation quaternions.
- 📦 **Rosbag Helper**: Quick commands and status overview for recording and playing ROS 2 bags.
- 📋 **Interfaces Explorer**: Browse ROS 2 message, service, and action definitions organized by package.
- ⌨️ **Keyboard-First & Vim Navigation**: Fast navigation with `1-8`, `j/k`, `w`, `r`, `e`, `c`, `g`, `?`, and `q`.

---

## Dependencies & Requirements

- **ROS 2** (Humble, Iron, Jazzy, Rolling, or compatible distribution)
- **C++17 Compiler** (GCC >= 9.0 or Clang >= 10.0)
- **CMake** (>= 3.14)
- **Python 3 Development Headers** (for plugin engine)

### Third-Party Submodules (Included in `third_party/`)

The repository uses Git submodules for core C++ dependencies located under `third_party/`:
- [FTXUI v7.0.3](file:///home/u/source/open_source/lazyrtui/third_party/FTXUI_v7.0.3) — Terminal UI framework
- [yaml-cpp 0.9.0](file:///home/u/source/open_source/lazyrtui/third_party/yaml-cpp_0.9.0) — YAML parser and emitter
- [nlohmann_json v3.12.0](file:///home/u/source/open_source/lazyrtui/third_party/nlohmann_json_v3.12.0) — JSON library for C++

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

### 3. Run

Launch the compiled binary:
```bash
./run.sh
```
Or launch specifically in ROS 2 colcon mode:
```bash
./run.sh --ros2
```

---

## Keybindings

| Key | Action |
| :---: | :--- |
| `1` - `8` | Switch Tabs (Nodes, Topics, Services, Actions, Interfaces, Bags, TF, About) |
| `w` | Toggle pane focus (Left / Right Pane) |
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
├── CMakeLists.txt              # CMake build configuration using third_party submodules
├── package.xml                 # ROS 2 package manifest (ament_cmake)
├── DESIGN.md                   # Single source of design authority & architecture spec
├── AGENTS.md                   # Developer & AI Agent guidelines
├── TODO.md                     # Roadmap and code review notes
├── build.sh                    # Auto-environment detection build script
├── run.sh                      # Application launcher script
├── config/                     # Configuration files (YAML keybindings & presets)
├── include/lazyrtui/           # C++ Header files
│   ├── app.hpp                 # TUI Application & UiSnapshot double-buffer definitions
│   ├── ros_manager.hpp         # ROS 2 single-node manager & graph queries
│   ├── config_loader.hpp       # Config parser
│   ├── tf_tree.hpp             # TF tree data structures
│   ├── python_plugin_engine.hpp # Python C-API plugin executor
│   └── ftxui_converter.hpp     # JSON UI Spec to FTXUI Element/Canvas converter
├── src/                        # C++ Source files
│   ├── main.cpp                # Main entrypoint & signal handling
│   ├── app.cpp                 # FTXUI layout, tabs, and event handling
│   ├── ros_manager.cpp         # ROS 2 rclcpp API encapsulation
│   ├── config_loader.cpp       # Config loader implementation
│   ├── tf_tree.cpp             # TF tree calculations
│   ├── python_plugin_engine.cpp # Python plugin engine implementation
│   └── ftxui_converter.cpp     # FTXUI converter implementation
├── test/                       # GTest unit test suite
└── third_party/                # Git submodules with explicit version tags
    ├── FTXUI_v7.0.3/
    ├── yaml-cpp_0.9.0/
    └── nlohmann_json_v3.12.0/
```

---

## Code Style

C++ formatting uses **LLVM style** (`clang-format -style=LLVM`):
- 2-space indentation, no tabs
- 80-column line limit
- Braces attached to control statements and functions
- Pointers/references aligned right (`int* p`)

---

## License

MIT License.
