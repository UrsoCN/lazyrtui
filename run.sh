#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Parse arguments for --ros2 / -r flag
BUILD_FLAG=""
RUN_COLCON=false
PASS_ARGS=()

for arg in "$@"; do
    case $arg in
        --ros2|-r)
            BUILD_FLAG="--ros2"
            RUN_COLCON=true
            ;;
        *)
            PASS_ARGS+=("$arg")
            ;;
    esac
done

# ----- Source ROS 2 environment -----
if [ -z "$ROS_DISTRO" ]; then
    if [ -d /opt/ros ]; then
        ROS_DISTRO="$(ls /opt/ros | sort | tail -n1)"
        source "/opt/ros/$ROS_DISTRO/setup.bash"
    fi
fi

COLCON_BIN="install/lazyrtui/lib/lazyrtui/lazyrtui"
CMAKE_BIN="build/lazyrtui"

# Trigger build if executable for the target mode does not exist
if [ "$RUN_COLCON" = true ] && [ ! -x "$COLCON_BIN" ]; then
    ./build.sh --ros2
elif [ "$RUN_COLCON" = false ] && [ ! -x "$CMAKE_BIN" ] && [ ! -x "$COLCON_BIN" ]; then
    ./build.sh
fi

# Determine which binary to launch
if [ "$RUN_COLCON" = true ] || { [ -x "$COLCON_BIN" ] && [ ! -x "$CMAKE_BIN" ]; }; then
    if [ -f "install/setup.bash" ]; then
        source "install/setup.bash"
    fi
    if [ -x "$COLCON_BIN" ]; then
        echo "Launching colcon-built binary ($COLCON_BIN)..."
        exec "$COLCON_BIN" "${PASS_ARGS[@]}"
    else
        echo "Error: colcon binary not found at $COLCON_BIN" >&2
        exit 1
    fi
elif [ -x "$CMAKE_BIN" ]; then
    echo "Launching CMake-built binary ($CMAKE_BIN)..."
    exec "$CMAKE_BIN" "${PASS_ARGS[@]}"
else
    echo "Error: Binary not found. Please run ./build.sh or ./build.sh --ros2 first." >&2
    exit 1
fi
