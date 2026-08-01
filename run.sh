#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Source ROS 2 environment if not already sourced
if [ -z "$ROS_DISTRO" ]; then
    for distro in jazzy humble rolling iron foxy; do
        if [ -f "/opt/ros/$distro/setup.bash" ]; then
            source "/opt/ros/$distro/setup.bash"
            break
        fi
    done
fi

# Auto-setup .venv if missing
if [ ! -d ".venv" ]; then
    ./scripts/setup_env.sh
fi

# Activate venv and run lazyrtui
source .venv/bin/activate
exec lazyrtui "$@"
