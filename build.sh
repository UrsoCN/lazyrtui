#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ----- Parse arguments -----
USE_COLCON=false
for arg in "$@"; do
    case $arg in
        --ros2|-r)
            USE_COLCON=true
            ;;
    esac
done

# ----- Source ROS 2 environment -----
if [ -z "$ROS_DISTRO" ]; then
    if [ -d /opt/ros ]; then
        ROS_DISTRO="$(ls /opt/ros | sort | tail -n1)"
    else
        echo "No ROS installation found under /opt/ros" >&2
        exit 1
    fi
fi

if [ -f "/opt/ros/$ROS_DISTRO/setup.bash" ]; then
    echo "Sourcing ROS 2 $ROS_DISTRO ..."
    source "/opt/ros/$ROS_DISTRO/setup.bash"
else
    echo "Cannot find /opt/ros/$ROS_DISTRO/setup.bash" >&2
    exit 1
fi

# ----- Build -----
if [ "$USE_COLCON" = true ]; then
    echo "Building using colcon ..."

    # Clean pure CMake build if present to avoid build structure conflicts
    if [ -f "build/CMakeCache.txt" ] && [ ! -d "build/lazyrtui" ]; then
        echo "Clearing non-colcon build artifacts..."
        rm -rf build install log
    fi

    colcon build --paths . --cmake-args -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPython3_EXECUTABLE=/usr/bin/python3

    echo ""
    echo "Build succeeded! Binary: install/lazyrtui/lib/lazyrtui/lazyrtui"
else
    echo "Building using CMake ..."

    # Clean colcon build if present to avoid build structure conflicts
    if [ -d "install" ] || [ -d "build/lazyrtui" ]; then
        echo "Clearing colcon build artifacts..."
        rm -rf build install log
    fi

    echo "Configuring ..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPython3_EXECUTABLE=/usr/bin/python3

    echo "Building ..."
    cmake --build build -j"$(nproc)"

    echo ""
    echo "Build succeeded! Binary: build/lazyrtui"
fi
