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

step "5/5 生成更新包（自动更新用）"
# 包结构必须是"解压后直接就是 exe 所在目录的内容"，
# 应用内更新会把 zip 解压覆盖到安装目录。
if [[ -f "$PROJECT_ROOT/docs/user-guide.md" ]]; then
    cp -f "$PROJECT_ROOT/docs/user-guide.md" "$DIST_DIR/用户手册.md"
fi
touch "$DIST_DIR/portable.txt"

# 版本号从 CMakeLists 的 project(VERSION) 读，和程序内版本保持一致
APP_VERSION="$(sed -nE 's/^project\(FCapture VERSION ([0-9.]+).*/\1/p' \
    "$PROJECT_ROOT/CMakeLists.txt" | head -1)"
if [[ -z "$APP_VERSION" ]]; then
    APP_VERSION="1.0.0"
fi

# 命名约定必须与 UpdateChecker::assetNamePattern() 一致：
#   F-Capture-<version>-portable.zip
ZIP_NAME="F-Capture-${APP_VERSION}-portable.zip"
rm -f "$PROJECT_ROOT/dist/$ZIP_NAME"

WIN_SRC="$(cygpath -w "$PROJECT_ROOT/$DIST_DIR" 2>/dev/null || echo "$PROJECT_ROOT/$DIST_DIR")"
WIN_DST="$(cygpath -w "$PROJECT_ROOT/dist/$ZIP_NAME" 2>/dev/null || echo "$PROJECT_ROOT/dist/$ZIP_NAME")"
# -Path 传目录会多带一层目录名；用通配符把目录内容放到 zip 根
powershell -NoProfile -Command     "Compress-Archive -Path '$WIN_SRC\*' -DestinationPath '$WIN_DST' -Force"

ZIP_FILE="$PROJECT_ROOT/dist/$ZIP_NAME"
if [[ -f "$ZIP_FILE" ]]; then
    echo "[build] 更新包: $ZIP_FILE ($(du -h "$ZIP_FILE" | cut -f1))"

    # 输出 SHA256 —— 发布到 GitHub Release 时把这一行写进 release 说明，
    # 客户端更新时会据此校验下载完整性（UpdateChecker::parseSha256FromBody）。
    SHA256="$(powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '$WIN_DST').Hash.ToLower()" | tr -d '')"
    echo ""
    echo "[build] ====== 发布用信息 ======"
    echo "[build] 资产文件: $ZIP_NAME"
    echo "[build] SHA256:   $SHA256"
    echo "[build] 请把下一行写进 GitHub Release 说明："
    echo "[build] SHA256: $SHA256"
    echo "[build] ========================"
    echo ""
    if [[ -n "$SHA256" ]]; then
        echo "SHA256: $SHA256" > "$PROJECT_ROOT/dist/SHA256.txt"
    fi
else
    echo "[build] WARN: 更新包未生成" >&2
fi

echo ""
echo "=========================================="
echo "[build] 构建完成"
echo "=========================================="
echo "可执行文件: $PROJECT_ROOT/$DIST_DIR/$EXE_NAME"
echo "运行冒烟测试: ./scripts/smoke-test.sh"
