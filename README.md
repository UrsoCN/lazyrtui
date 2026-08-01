# LazyRTUI

A terminal user interface (TUI) for operating and monitoring ROS 2 nodes, topics, services, actions, interfaces, and bags efficiently.

## Features
- **Tabbed Layout**: Dedicated tabs for Nodes, Topics, Services, Actions, Interfaces, and Bags.
- **Keyboard-First Design**: Optimized for mouse-less / SSH terminal environments.
- **Clean Log Policy**: Uses a single persistent ROS 2 node to prevent `~/.ros/log` directory clutter.
- **Customizable**: Decoupled layout and user plugins for topic plotting and service parameter presets.

## Quick Start

### 1. Auto Setup Environment
Run the setup script to create `.venv` with `--system-site-packages` (so ROS 2 packages like `rclpy` are inherited seamlessly):
```bash
./scripts/setup_env.sh
```

### 2. Run LazyRTUI
You can launch LazyRTUI via the top-level launcher script:
```bash
./run.sh
```
Or source your ROS 2 environment and run `lazyrtui`:
```bash
source /opt/ros/jazzy/setup.bash
source .venv/bin/activate
lazyrtui
```
