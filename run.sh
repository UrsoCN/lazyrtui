#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -f "build/lazyrtui" ]; then
    ./build.sh
fi

# Source ROS 2 environment
if [ -z "$ROS_DISTRO" ]; then
    if [ -d /opt/ros ]; then
        ROS_DISTRO="$(ls /opt/ros | sort | tail -n1)"
        source "/opt/ros/$ROS_DISTRO/setup.bash"
    fi
fi

exec ./build/lazyrtui "$@"
