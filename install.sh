#!/bin/bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
INSTALL_BIN="${INSTALL_BIN:-${PREFIX}/bin/esh}"

if [ "${1:-}" = "--system-bin" ]; then
  INSTALL_BIN="/usr/bin/esh"
elif [ "${1:-}" = "--local-bin" ]; then
  INSTALL_BIN="/usr/local/bin/esh"
elif [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./install.sh [--local-bin|--system-bin]

Builds esh and installs the binary.

Defaults:
  PREFIX=/usr/local
  INSTALL_BIN=$PREFIX/bin/esh
  BUILD_TYPE=Release

Examples:
  sudo ./install.sh
  sudo ./install.sh --system-bin
  sudo PREFIX=/usr ./install.sh
  INSTALL_BIN="$HOME/.local/bin/esh" ./install.sh
EOF
  exit 0
fi

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ]; then
  user_name="${SUDO_USER}"
  user_home="$(getent passwd "${SUDO_USER}" | cut -d: -f6)"
else
  user_name="$(id -un)"
  user_home="${HOME}"
fi

config_home="${XDG_CONFIG_HOME:-${user_home}/.config}"
data_home="${XDG_DATA_HOME:-${user_home}/.local/share}"

echo "1. build ${BUILD_TYPE}"
cmake -S "${repo_dir}" -B "${repo_dir}/build" -D CMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${repo_dir}/build" -j

echo "2. install binary"
install -Dm755 "${repo_dir}/build/shell" "${INSTALL_BIN}"

echo "3. create user files"
install -d "${config_home}/esh/scripts"
install -d "${data_home}/esh/scripts"
install -d "${data_home}/esh"

if [ ! -f "${config_home}/esh/eshrc" ]; then
  : >"${config_home}/esh/eshrc"
fi

if [ ! -f "${data_home}/esh/history" ]; then
  : >"${data_home}/esh/history"
fi

if [ "$(id -u)" = "0" ]; then
  chown -R "${user_name}:${user_name}" "${config_home}/esh" "${data_home}/esh"
fi

echo "installed ${INSTALL_BIN}"
echo "config:  ${config_home}/esh/eshrc"
echo "scripts: ${config_home}/esh/scripts"
echo "scripts: ${data_home}/esh/scripts"
echo "history: ${data_home}/esh/history"
