# LazyRTUI

A terminal user interface (TUI) for operating and monitoring ROS 2 nodes, topics, services, actions, interfaces, and bags efficiently.

## Features
- **Tabbed Layout**: Dedicated tabs for Nodes, Topics, Services, Actions, Interfaces, and Bags.
- **Keyboard-First Design**: Optimized for mouse-less / SSH terminal environments.
- **Clean Log Policy**: Uses a single persistent ROS 2 node to prevent `~/.ros/log` directory clutter.
- **Customizable**: Decoupled layout and user plugins for topic plotting and service parameter presets.

## Requirements
- ROS 2 (Humble/Jazzy)
- CMake 3.14+
- C++17 compiler

## Quick Start

### 1. Build LazyRTUI
Configure and build the project using CMake:
```bash
mkdir build
cd build
cmake ..
make
```

### 2. Run LazyRTUI
You can launch LazyRTUI via the top-level launcher script, which will automatically source ROS 2 and build if necessary:
```bash
./run.sh
```
Or source your ROS 2 environment and run the executable directly:
```bash
source /opt/ros/jazzy/setup.bash
./build/lazyrtui
```
