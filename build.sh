#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

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
echo "Configuring ..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "Building ..."
cmake --build build -j"$(nproc)"

echo ""
echo "Build succeeded!  Binary: build/lazyrtui"
