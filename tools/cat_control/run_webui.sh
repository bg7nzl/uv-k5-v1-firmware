#!/usr/bin/env bash
# CAT Control Web UI 启动脚本
#
# 用法:
#   ./run_webui.sh                       # 默认 0.0.0.0:8765
#   ./run_webui.sh -p 9000               # 指定端口
#   ./run_webui.sh -h 127.0.0.1 -p 9000  # 指定 host 和端口
#   CAT_WEB_PORT=9000 ./run_webui.sh     # 也支持环境变量
#
# 首次运行会在 tools/cat_control/.venv 下创建虚拟环境并安装 requirements.txt。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--host) CAT_WEB_HOST="$2"; shift 2 ;;
    -p|--port) CAT_WEB_PORT="$2"; shift 2 ;;
    --help)
      sed -n '2,10p' "$0"
      exit 0
      ;;
    *)
      echo "未知参数: $1" >&2
      exit 1
      ;;
  esac
done

export CAT_WEB_HOST="${CAT_WEB_HOST:-0.0.0.0}"
export CAT_WEB_PORT="${CAT_WEB_PORT:-8765}"

PY="${PYTHON:-python3}"
VENV_DIR="$SCRIPT_DIR/.venv"

if [[ ! -d "$VENV_DIR" ]]; then
  echo "[setup] 创建虚拟环境: $VENV_DIR"
  "$PY" -m venv "$VENV_DIR"
fi

# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

REQ_STAMP="$VENV_DIR/.requirements.stamp"
if [[ ! -f "$REQ_STAMP" || "$SCRIPT_DIR/requirements.txt" -nt "$REQ_STAMP" ]]; then
  echo "[setup] 安装依赖: requirements.txt"
  pip install --quiet --upgrade pip
  pip install --quiet -r "$SCRIPT_DIR/requirements.txt"
  touch "$REQ_STAMP"
fi

echo "[run] CAT Web UI -> http://${CAT_WEB_HOST}:${CAT_WEB_PORT}/"
exec python webui/server.py
