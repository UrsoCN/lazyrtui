#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

echo "=================================================="
echo "  LazyRTUI Environment Auto-Setup Script          "
echo "=================================================="

# 1. Detect and Source ROS 2 Environment
if [ -n "$ROS_DISTRO" ]; then
    echo "[✓] Active ROS 2 environment detected: ROS_DISTRO=$ROS_DISTRO"
else
    ROS_SETUP=""
    for distro in jazzy humble rolling iron foxy; do
        if [ -f "/opt/ros/$distro/setup.bash" ]; then
            ROS_SETUP="/opt/ros/$distro/setup.bash"
            break
        fi
    done

    if [ -n "$ROS_SETUP" ]; then
        echo "[i] Sourcing ROS 2 environment from $ROS_SETUP ..."
        source "$ROS_SETUP"
    else
        echo "[!] Warning: No ROS 2 installation found in /opt/ros/. Running in offline / demo mode."
    fi
fi

# 2. Check/Create .venv with --system-site-packages
if [ ! -d ".venv" ]; then
    echo "[i] Creating virtual environment (.venv) with --system-site-packages ..."
    python3 -m venv --system-site-packages .venv
else
    # Check if .venv includes system-site-packages
    PYVENV_CFG=".venv/pyvenv.cfg"
    if [ -f "$PYVENV_CFG" ] && ! grep -q "include-system-site-packages = true" "$PYVENV_CFG"; then
        echo "[i] Re-configuring .venv to include system site-packages ..."
        python3 -m venv --system-site-packages .venv
    else
        echo "[✓] Virtual environment (.venv) is ready."
    fi
fi

# 3. Install / Update Dependencies
echo "[i] Checking and installing Python dependencies ..."
.venv/bin/pip install --quiet textual plotext pyyaml setuptools
.venv/bin/pip install --quiet --no-build-isolation -e .

echo "=================================================="
echo "  Setup Complete! You can now run:"
echo "    ./run.sh  or  lazyrtui"
echo "=================================================="
