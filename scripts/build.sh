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
# --compiler-runtime：把 MSVC 运行时（MSVCP140 / VCRUNTIME140 系列）一并复制。
# 不加这一项时，windeployqt 在 Release 模式下默认不收集它们，程序在
# 未安装 VC++ 2015-2022 Redistributable 的机器上会报"缺少 MSVCP140.dll"。
"$WINDEPLOYQT" --release --compiler-runtime --no-translations \
    --no-system-d3d-compiler --no-opengl-sw "$PROJECT_ROOT/$DIST_DIR/$EXE_NAME"
if [[ $? -ne 0 ]]; then
    echo "[build] FAIL: windeployqt 失败" >&2
    exit 1
fi

if [[ ! -f "$DIST_DIR/platforms/qwindows.dll" ]]; then
    echo "[build] FAIL: platforms/qwindows.dll 缺失，程序无法启动" >&2
    exit 1
fi

# MSVC 运行时：windeployqt 的 --compiler-runtime 在本机实测不生效
# （Qt 6 从 VS 安装目录找 redist 时路径没命中），直接自己复制。
# 缺这些 DLL 时，未安装 VC++ 2015-2022 Redistributable 的机器会报
# "缺少 MSVCP140.dll" 直接起不来。
CRT_SRC=""
for candidate in \
    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC" \
    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Redist/MSVC" \
    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Redist/MSVC"; do
    if [[ -d "$candidate" ]]; then
        # 只认纯数字版本目录（如 14.44.35112）：同级的 v143 是符号链接目录，
        # 里面没有 x64/Microsoft.VC143.CRT，sort -V 会误选它。
        for ver in $(ls -1 "$candidate" 2>/dev/null | grep -E '^[0-9]+\.' | sort -V -r); do
            if [[ -d "$candidate/$ver/x64/Microsoft.VC143.CRT" ]]; then
                CRT_SRC="$candidate/$ver/x64/Microsoft.VC143.CRT"
                break
            fi
        done
        [[ -n "$CRT_SRC" ]] && break
    fi
done

if [[ -n "$CRT_SRC" ]]; then
    for dll in msvcp140.dll msvcp140_1.dll msvcp140_2.dll msvcp140_atomic_wait.dll \
               vcruntime140.dll vcruntime140_1.dll concrt140.dll; do
        cp -f "$CRT_SRC/$dll" "$DIST_DIR/" 2>/dev/null || true
    done
    echo "[build] 已复制 MSVC 运行时: $CRT_SRC"
else
    echo "[build] WARN: 找不到 VC++ Redist 目录，运行时 DLL 未打包" >&2
fi

# 兜底校验：这三个是 exe / Qt / Ela 都依赖的核心运行时
for dll in msvcp140.dll vcruntime140.dll vcruntime140_1.dll; do
    if [[ ! -f "$DIST_DIR/$dll" ]]; then
        echo "[build] FAIL: $dll 缺失，未装 VC++ Redist 的机器无法启动" >&2
        exit 1
    fi
done

# ElaWidgetTools.dll 是 IMPORTED 目标，windeployqt 不会自动收集，必须手动复制
ELA_DLL="D:/cppsoft/MsvcEla/bin/ElaWidgetTools.dll"
if [[ ! -f "$ELA_DLL" ]]; then
    echo "[build] FAIL: 找不到 $ELA_DLL" >&2
    exit 1
fi
cp -f "$ELA_DLL" "$DIST_DIR/"
echo "[build] 已复制: $ELA_DLL → $DIST_DIR/"

step "5/5 生成发布包"

if [[ -f "$PROJECT_ROOT/docs/user-guide.md" ]]; then
    cp -f "$PROJECT_ROOT/docs/user-guide.md" "$DIST_DIR/用户手册.md"
fi

# 版本号从 CMakeLists 的 project(VERSION) 读，和程序内版本保持一致
APP_VERSION="$(sed -nE 's/^project\(FCapture VERSION ([0-9.]+).*/\1/p' \
    "$PROJECT_ROOT/CMakeLists.txt" | head -1)"
if [[ -z "$APP_VERSION" ]]; then
    APP_VERSION="1.0.0"
fi

# ---------------------------------------------------------------- 便携包
# 便携包带 portable.txt，配置落在 data/ 子目录，拷走即可。
# 命名约定必须与 UpdateChecker::assetNamePattern() 一致：
#   F-Capture-<version>-portable.zip
touch "$DIST_DIR/portable.txt"

ZIP_NAME="F-Capture-${APP_VERSION}-portable.zip"
rm -f "$PROJECT_ROOT/dist/$ZIP_NAME"

WIN_SRC="$(cygpath -w "$PROJECT_ROOT/$DIST_DIR" 2>/dev/null || echo "$PROJECT_ROOT/$DIST_DIR")"
WIN_DST="$(cygpath -w "$PROJECT_ROOT/dist/$ZIP_NAME" 2>/dev/null || echo "$PROJECT_ROOT/dist/$ZIP_NAME")"
# -Path 传目录会多带一层目录名；用通配符把目录内容放到 zip 根
powershell -NoProfile -Command \
    "Compress-Archive -Path '$WIN_SRC\*' -DestinationPath '$WIN_DST' -Force"

ZIP_FILE="$PROJECT_ROOT/dist/$ZIP_NAME"
if [[ -f "$ZIP_FILE" ]]; then
    echo "[build] 便携包: $ZIP_FILE ($(du -h "$ZIP_FILE" | cut -f1))"

    # 输出 SHA256 —— 发布到 GitHub Release 时把这一行写进 release 说明，
    # 客户端更新时会据此校验下载完整性（UpdateChecker::parseSha256FromBody）。
    SHA256="$(powershell -NoProfile -Command \
        "(Get-FileHash -Algorithm SHA256 -LiteralPath '$WIN_DST').Hash.ToLower()" | tr -d '\r')"
    echo ""
    echo "[build] ====== 发布用信息 ======"
    echo "[build] 资产文件: $ZIP_NAME"
    echo "[build] 请把下一行写进 GitHub Release 说明："
    echo "[build] SHA256: $SHA256"
    echo "[build] ========================"
    echo ""
    if [[ -n "$SHA256" ]]; then
        echo "SHA256: $SHA256" > "$PROJECT_ROOT/dist/SHA256.txt"
    fi
else
    echo "[build] WARN: 便携包未生成" >&2
fi

# ---------------------------------------------------------------- 安装包
# 安装版**不能**带 portable.txt，否则配置会写进 Program Files（无权限）。
# iss 脚本里已经明确不打包该文件，这里把 dist 里的删掉更保险。
rm -f "$DIST_DIR/portable.txt"

ISCC=""
for candidate in \
    "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "C:/Program Files/Inno Setup 6/ISCC.exe" \
    "D:/Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "D:/Program Files/Inno Setup 6/ISCC.exe"; do
    if [[ -f "$candidate" ]]; then
        ISCC="$candidate"
        break
    fi
done

if [[ -n "$ISCC" ]]; then
    echo "[build] 使用 Inno Setup: $ISCC"
    "$ISCC" //Qp "$PROJECT_ROOT/installer/FCapture.iss"
    if [[ $? -eq 0 ]]; then
        SETUP_FILE="$PROJECT_ROOT/dist/installer/F-Capture-${APP_VERSION}-setup.exe"
        if [[ -f "$SETUP_FILE" ]]; then
            echo "[build] 安装包: $SETUP_FILE ($(du -h "$SETUP_FILE" | cut -f1))"
        fi
    else
        echo "[build] WARN: Inno Setup 编译失败" >&2
    fi
else
    echo "[build] SKIP: 未找到 Inno Setup 6（ISCC.exe）"
    echo "[build]       安装后可用以下命令单独打包："
    echo "[build]       \"C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe\" installer\\FCapture.iss"
fi

echo ""
echo "=========================================="
