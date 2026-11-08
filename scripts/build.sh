#!/usr/bin/env bash
# F-Capture 一键构建 + 打包
# 用法: ./scripts/build.sh [clean]
#   clean  先删除 build/ 和 dist/ 再重新构建
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

QT_DIR="D:/Qt/6.8.3/msvc2022_64"
CMAKE="D:/CMake/bin/cmake"
WINDEPLOYQT="$QT_DIR/bin/windeployqt.exe"

BUILD_DIR="build"
DIST_DIR="dist/F-Capture"
EXE_NAME="FCapture.exe"

if [[ "${1:-}" == "clean" ]]; then
    echo "[build] 清理 build/ dist/"
    rm -rf "$BUILD_DIR" "$DIST_DIR"
fi

step() {
    echo ""
    echo "=========================================="
    echo "[build] $1"
    echo "=========================================="
}

step "1/4 配置 (Visual Studio 17 2022 x64)"
"$CMAKE" -S . -B "$BUILD_DIR" \
    -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_PREFIX_PATH="$QT_DIR"
if [[ $? -ne 0 ]]; then
    echo "[build] FAIL: 配置失败" >&2
    exit 1
fi

step "2/4 编译 (Release)"
"$CMAKE" --build "$BUILD_DIR" --config Release
if [[ $? -ne 0 ]]; then
    echo "[build] FAIL: 编译失败" >&2
    exit 1
fi

step "3/4 复制到 dist"
mkdir -p "$DIST_DIR"
BUILT_EXE=$(find "$BUILD_DIR" -name "$EXE_NAME" -path "*Release*" | head -1)
if [[ -z "$BUILT_EXE" ]]; then
    echo "[build] FAIL: 找不到 $EXE_NAME（检查 CMakeLists.txt 的 OUTPUT_NAME / WIN32 设置）" >&2
    exit 1
fi
cp -f "$BUILT_EXE" "$DIST_DIR/"
echo "[build] 已复制: $BUILT_EXE → $DIST_DIR/"

step "4/4 windeployqt 收集依赖"
"$WINDEPLOYQT" --release --no-translations --no-system-d3d-compiler \
    --no-opengl-sw "$PROJECT_ROOT/$DIST_DIR/$EXE_NAME"
if [[ $? -ne 0 ]]; then
    echo "[build] FAIL: windeployqt 失败" >&2
    exit 1
fi

if [[ ! -f "$DIST_DIR/platforms/qwindows.dll" ]]; then
    echo "[build] FAIL: platforms/qwindows.dll 缺失，程序无法启动" >&2
    exit 1
fi

# ElaWidgetTools.dll 是 IMPORTED 目标，windeployqt 不会自动收集，必须手动复制
ELA_DLL="D:/cppsoft/MsvcEla/bin/ElaWidgetTools.dll"
if [[ ! -f "$ELA_DLL" ]]; then
    echo "[build] FAIL: 找不到 $ELA_DLL" >&2
    exit 1
fi
cp -f "$ELA_DLL" "$DIST_DIR/"
echo "[build] 已复制: $ELA_DLL → $DIST_DIR/"

step "5/5 生成便携版 zip"
# 便携包：exe + 依赖 + 用户手册 + portable.txt（开箱即用，配置落在 data/ 下）
if [[ -f "$PROJECT_ROOT/docs/user-guide.md" ]]; then
    cp -f "$PROJECT_ROOT/docs/user-guide.md" "$DIST_DIR/用户手册.md"
fi
touch "$DIST_DIR/portable.txt"

ZIP_NAME="F-Capture-1.0.0-portable.zip"
rm -f "$PROJECT_ROOT/dist/$ZIP_NAME"
# 本机没有 zip 命令，用 PowerShell 的 Compress-Archive。
# 注意：PowerShell 不认 /d/... 这种 MSYS 路径，必须先转成 Windows 路径。
WIN_SRC="$(cygpath -w "$PROJECT_ROOT/$DIST_DIR" 2>/dev/null || echo "$PROJECT_ROOT/$DIST_DIR")"
WIN_DST="$(cygpath -w "$PROJECT_ROOT/dist/$ZIP_NAME" 2>/dev/null || echo "$PROJECT_ROOT/dist/$ZIP_NAME")"
powershell -NoProfile -Command \
    "Compress-Archive -Path '$WIN_SRC' -DestinationPath '$WIN_DST' -Force"
if [[ -f "$PROJECT_ROOT/dist/$ZIP_NAME" ]]; then
    echo "[build] 便携包: $PROJECT_ROOT/dist/$ZIP_NAME ($(du -h "$PROJECT_ROOT/dist/$ZIP_NAME" | cut -f1))"
else
    echo "[build] WARN: 便携包未生成" >&2
fi

echo ""
echo "=========================================="
echo "[build] 构建完成"
echo "=========================================="
echo "可执行文件: $PROJECT_ROOT/$DIST_DIR/$EXE_NAME"
echo "运行冒烟测试: ./scripts/smoke-test.sh"
