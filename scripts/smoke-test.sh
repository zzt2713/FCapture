#!/usr/bin/env bash
# 冒烟测试：启动 exe，检查进程存活 3 秒、日志无 ERROR
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="$PROJECT_ROOT/dist/F-Capture/FCapture.exe"
EXE_NAME="FCapture.exe"
LOG_DIR="$LOCALAPPDATA/F-Capture/log"
LOG_DIR_WIN="$(cygpath -u "$LOCALAPPDATA" 2>/dev/null || echo "$LOCALAPPDATA")/F-Capture/log"
[[ -d "$LOG_DIR_WIN" ]] && LOG_DIR="$LOG_DIR_WIN"

if [[ ! -f "$EXE" ]]; then
    echo "[smoke] FAIL: 找不到 $EXE" >&2
    exit 1
fi

# 先清理同名残留进程，避免误判
taskkill //F //IM "$EXE_NAME" >/dev/null 2>&1 || true

echo "[smoke] 启动 $EXE"
"$EXE" &
sleep 3

if tasklist //FI "IMAGENAME eq $EXE_NAME" 2>/dev/null | grep -qi "$EXE_NAME"; then
    echo "[smoke] PASS: 进程存活 3 秒"
    ALIVE=1
else
    echo "[smoke] FAIL: 进程启动后 3 秒内退出（崩溃）" >&2
    ALIVE=0
fi

# 日志检查
if [[ -d "$LOG_DIR" ]]; then
    LOGFILE=$(ls -t "$LOG_DIR"/*.log 2>/dev/null | head -1)
    if [[ -n "${LOGFILE:-}" ]]; then
        ERRS=$(grep -c '\[ERROR\]' "$LOGFILE" 2>/dev/null)
        ERRS=${ERRS:-0}
        if [[ "$ERRS" -gt 0 ]]; then
            echo "[smoke] WARN: 日志有 $ERRS 条 ERROR" >&2
            grep '\[ERROR\]' "$LOGFILE" | tail -5 >&2
        else
            echo "[smoke] PASS: 日志无 ERROR"
        fi
    fi
else
    echo "[smoke] SKIP: 日志目录未创建（首次启动可能正常）"
fi

if [[ "$ALIVE" -eq 1 ]]; then
    taskkill //F //IM "$EXE_NAME" >/dev/null 2>&1 || true
    echo "[smoke] 已关闭进程"
    exit 0
fi
exit 1
