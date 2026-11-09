cmake_minimum_required(VERSION 3.21)
project(FCapture VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

qt_standard_project_setup()

# ---------------------------------------------------------------- ElaWidgetTools
# 2.0.2 预编译版（ABI 已核对兼容 Qt 6.8.3 msvc2022_64）。
# 官方 Config.cmake 引用了不存在的 Targets.cmake，这里手写 IMPORTED 目标接入。
set(ELA_ROOT "D:/cppsoft/MsvcEla" CACHE PATH "ElaWidgetTools 2.0.2 root")

if(NOT EXISTS "${ELA_ROOT}/lib/ElaWidgetTools.lib")
    message(FATAL_ERROR "找不到 ${ELA_ROOT}/lib/ElaWidgetTools.lib，检查 ELA_ROOT")
endif()

add_library(ElaWidgetTools SHARED IMPORTED GLOBAL)
set_target_properties(ElaWidgetTools PROPERTIES
    IMPORTED_IMPLIB "${ELA_ROOT}/lib/ElaWidgetTools.lib"
    IMPORTED_LOCATION "${ELA_ROOT}/bin/ElaWidgetTools.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ELA_ROOT}/include"
)

# 应用源文件（不含 main.cpp）：抽成静态库，exe 与各验证工具共用同一份实现，
# 避免测试目标漏掉源文件导致链接失败。
set(APP_SOURCES
    src/core/AppController.cpp
    src/core/AppPaths.cpp
    src/core/ClipboardHandler.cpp
    src/core/CommandLine.cpp
    src/core/ConfigManager.cpp
    src/core/Diagnostics.cpp
    src/core/UpdateChecker.cpp
    src/core/UpdateInstaller.cpp
    src/core/ImageOutput.cpp
    src/core/Watchdog.cpp
    src/core/ZipStoreWriter.cpp
    src/core/HotkeyManager.cpp
    src/core/Logger.cpp
    src/core/SingleInstance.cpp
    src/core/TrayController.cpp

    src/annotate/Annotation.cpp
    src/annotate/AnnotationModel.cpp
    src/annotate/AnnotationTool.cpp
    src/annotate/Palette.cpp
    src/annotate/PaintBar.cpp
    src/annotate/TextInputBox.cpp
    src/annotate/shapes/FreehandShape.cpp
    src/annotate/shapes/LineShape.cpp
    src/annotate/shapes/MagnifyShape.cpp
    src/annotate/shapes/NumberShape.cpp
    src/annotate/shapes/PixelShape.cpp
    src/annotate/shapes/RectShapes.cpp
    src/annotate/shapes/TextShape.cpp

    src/paste/PinContextMenu.cpp
    src/paste/PinManager.cpp
    src/paste/PinWindow.cpp

    src/snip/ImageEffects.cpp
    src/snip/LongShotBar.cpp
    src/snip/LongShotStitcher.cpp
    src/snip/ScreenCapturer.cpp
    src/snip/SizeInputBox.cpp
    src/snip/SnipHistory.cpp
    src/snip/SnipOverlay.cpp
    src/snip/SnipOverlayAnnotate.cpp
    src/snip/SnipOverlayEdit.cpp

    src/platform/win/AppLauncher.cpp
    src/platform/win/ElementDetector.cpp
    src/platform/win/HotkeyHook.cpp
    src/platform/win/WinOcr.cpp
    src/platform/win/VirtualDesktop.cpp
    src/platform/win/WinUtil.cpp
    src/platform/win/WinCrashHandler.cpp
    src/platform/win/WinHotkey.cpp
    src/platform/win/WindowDetector.cpp
    src/platform/win/SoundPlayer.cpp

    src/tools/HotCorner.cpp
    src/tools/OcrEngine.cpp
    src/tools/TextToImage.cpp
    src/tools/Whiteboard.cpp

    src/ui/AppMenu.cpp
    src/ui/ElaThemeBridge.cpp
    src/ui/GroupManagerDialog.cpp
    src/ui/HotkeyRecorder.cpp
    src/ui/IconFactory.cpp
    src/ui/NavList.cpp
    src/ui/OcrResultDialog.cpp
    src/ui/OperationToast.cpp
    src/ui/PreferenceDialog.cpp
    src/ui/Theme.cpp
    src/ui/ToolIcons.cpp
)

set(HEADERS
    src/core/AppController.h
    src/core/AppPaths.h
    src/core/ClipboardHandler.h
    src/core/CommandLine.h
    src/core/ConfigManager.h
    src/core/Diagnostics.h
    src/core/UpdateChecker.h
    src/core/UpdateInstaller.h
    src/core/Watchdog.h
    src/core/ZipStoreWriter.h
    src/core/HotkeyManager.h
    src/core/ImageOutput.h
    src/core/Logger.h
    src/core/SingleInstance.h
    src/core/TrayController.h

    src/annotate/Annotation.h
    src/annotate/AnnotationModel.h
    src/annotate/AnnotationTool.h
    src/annotate/Palette.h
    src/annotate/PaintBar.h
    src/annotate/TextInputBox.h
    src/annotate/shapes/FreehandShape.h
    src/annotate/shapes/LineShape.h
    src/annotate/shapes/MagnifyShape.h
    src/annotate/shapes/NumberShape.h
    src/annotate/shapes/PixelShape.h
    src/annotate/shapes/RectShapes.h
    src/annotate/shapes/TextShape.h

    src/paste/PinContextMenu.h
    src/paste/PinManager.h
    src/paste/PinWindow.h

    src/snip/ImageEffects.h
    src/snip/LongShotBar.h
    src/snip/LongShotStitcher.h
    src/snip/ScreenCapturer.h
    src/snip/SizeInputBox.h
    src/snip/SnipHistory.h
    src/snip/SnipOverlay.h
    src/snip/SnipResult.h

    src/platform/win/AppLauncher.h
    src/platform/win/ElementDetector.h
    src/platform/win/HotkeyHook.h
    src/platform/win/WinOcr.h
    src/platform/win/VirtualDesktop.h
    src/platform/win/WinUtil.h
    src/platform/win/WinCrashHandler.h
    src/platform/win/WinHotkey.h
    src/platform/win/WindowDetector.h
    src/platform/win/SoundPlayer.h

    src/tools/HotCorner.h
    src/tools/OcrEngine.h
    src/tools/TextToImage.h
    src/tools/Whiteboard.h

    src/ui/AppMenu.h
    src/ui/ElaThemeBridge.h
    src/ui/GroupManagerDialog.h
    src/ui/HotkeyRecorder.h
    src/ui/IconFactory.h
    src/ui/NavList.h
    src/ui/OcrResultDialog.h
    src/ui/OperationToast.h
    src/ui/PreferenceDialog.h
    src/ui/Theme.h
    src/ui/ToolIcons.h
)

# 应用实现静态库：exe 与验证工具共用，测试目标只需链接它 + 自己的 main.cpp
add_library(fcapture_core STATIC ${APP_SOURCES} ${HEADERS})

# 版本号单一来源：project(FCapture VERSION x.y.z)，编译期注入 C++ 侧
target_compile_definitions(fcapture_core PUBLIC
    FCAPTURE_VERSION="${PROJECT_VERSION}"
)
target_include_directories(fcapture_core PUBLIC src)
target_link_libraries(fcapture_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    ElaWidgetTools
)

if(MSVC)
    target_compile_options(fcapture_core PUBLIC /W4 /utf-8 /permissive-)
    # Win32 API: user32 / shell32 / dwmapi / oleacc(MSAA) / ole32+oleaut32(COM) / advapi32(注册表)
    target_link_libraries(fcapture_core PUBLIC
        user32 shell32 dwmapi oleacc ole32 oleaut32 advapi32
        windowsapp runtimeobject winmm)
    # C++/WinRT 需要 /await 才能用 co_await（这里主要用 .get()，加上更稳）
    target_compile_options(fcapture_core PUBLIC /await)
endif()

if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

qt_add_executable(FCapture WIN32 src/main.cpp ${SOURCES})
target_link_libraries(FCapture PRIVATE fcapture_core)

# ElaWidgetTools.dll 必须和 exe 同目录，否则启动即崩
add_custom_command(TARGET FCapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ELA_ROOT}/bin/ElaWidgetTools.dll"
            "$<TARGET_FILE_DIR:FCapture>"
    COMMENT "Copy ElaWidgetTools.dll"
)

add_custom_command(TARGET FCapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:FCapture>/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/result.mp3"
            "$<TARGET_FILE_DIR:FCapture>/sounds/result.mp3"
    COMMENT "Copy completion sound"
)

qt_add_resources(FCapture "app_resources"
    PREFIX "/"
    BASE resources
    FILES
        resources/icon.png
        resources/result.mp3
)

qt_add_resources(FCapture "icon_resources"
    PREFIX "/icons"
    BASE resources/icons
    FILES
        resources/icons/square.svg
        resources/icons/circle.svg
        resources/icons/minus.svg
        resources/icons/arrow-up-right.svg
        resources/icons/spline.svg
        resources/icons/hash.svg
        resources/icons/lasso-select.svg
        resources/icons/scan-search.svg
        resources/icons/line-style.svg
        resources/icons/arrow-left-right.svg
        resources/icons/pencil.svg
        resources/icons/highlighter.svg
        resources/icons/type.svg
        resources/icons/grid-3x3.svg
        resources/icons/circle-dashed.svg
        resources/icons/eraser.svg
        resources/icons/undo-2.svg
        resources/icons/redo-2.svg
        resources/icons/x.svg
        resources/icons/trash-2.svg
        resources/icons/copy.svg
        resources/icons/save.svg
        resources/icons/pin.svg
        resources/icons/send.svg
        resources/icons/scan-text.svg
        resources/icons/qr-code.svg
        resources/icons/check.svg
        resources/icons/play.svg
        resources/icons/pause.svg
)

# 头文件路径、Qt 模块、Win32 库、编译选项都由 fcapture_core 的 PUBLIC 属性传递

set_target_properties(FCapture PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "FCapture"
)

# ---------------------------------------------------------------- 开发工具
# 菜单渲染预览（不打包进发行版，仅用于离屏验证菜单外观）
option(BUILD_MENU_PREVIEW "Build the menu render preview tool" OFF)

if(BUILD_MENU_PREVIEW)
    add_executable(menupreview
        tools/menupreview/main.cpp
    )
    target_include_directories(menupreview PRIVATE src)
    target_link_libraries(menupreview PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(menupreview PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(menupreview "preview_resources"
        PREFIX "/"
        BASE resources
        FILES resources/icon.png
    )
endif()

# 标注渲染预览
option(BUILD_ANNOTATE_PREVIEW "Build the annotation render preview tool" OFF)

if(BUILD_ANNOTATE_PREVIEW)
    add_executable(annotatepreview
        tools/annotatepreview/main.cpp
    )
    target_include_directories(annotatepreview PRIVATE src)
    target_link_libraries(annotatepreview PRIVATE fcapture_core)
    qt_add_resources(annotatepreview "preview_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg
            resources/icons/circle.svg
            resources/icons/minus.svg
            resources/icons/arrow-up-right.svg
            resources/icons/spline.svg
            resources/icons/hash.svg
            resources/icons/lasso-select.svg
            resources/icons/scan-search.svg
            resources/icons/line-style.svg
            resources/icons/arrow-left-right.svg
            resources/icons/pencil.svg
            resources/icons/highlighter.svg
            resources/icons/type.svg
            resources/icons/grid-3x3.svg
            resources/icons/circle-dashed.svg
            resources/icons/eraser.svg
            resources/icons/undo-2.svg
            resources/icons/redo-2.svg
            resources/icons/x.svg
        resources/icons/trash-2.svg
            resources/icons/copy.svg
            resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
    )
    if(MSVC)
        target_compile_options(annotatepreview PRIVATE /W4 /utf-8 /permissive-)
    endif()
endif()

# 文字渲染预览
option(BUILD_TEXT_PREVIEW "Build the text render preview tool" OFF)

if(BUILD_TEXT_PREVIEW)
    add_executable(textpreview
        tools/textpreview/main.cpp
    )
    target_include_directories(textpreview PRIVATE src)
    target_link_libraries(textpreview PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(textpreview PRIVATE /W4 /utf-8 /permissive-)
    endif()
endif()

# 矩形模式滤镜验证
option(BUILD_PIXEL_TEST "Build the pixel shape rect-mode test" OFF)

if(BUILD_PIXEL_TEST)
    add_executable(pixeltest
        tools/pixeltest/main.cpp
    )
    target_include_directories(pixeltest PRIVATE src)
    target_link_libraries(pixeltest PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(pixeltest PRIVATE /W4 /utf-8 /permissive-)
    endif()
endif()

# 退出稳定性验证（贴图窗口清理）
option(BUILD_EXIT_TEST "Build the clean-exit test" OFF)

if(BUILD_EXIT_TEST)
    add_executable(exittest
        tools/exittest/main.cpp
    )
    target_include_directories(exittest PRIVATE src)
    target_link_libraries(exittest PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(exittest PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(exittest "exittest_resources"
        PREFIX "/"
        BASE resources
        FILES resources/icon.png
    )
endif()

# 贴图系统行为验证
option(BUILD_PIN_TEST "Build the pin window behaviour test" OFF)

if(BUILD_PIN_TEST)
    add_executable(pintest
        tools/pintest/main.cpp
    )
    target_include_directories(pintest PRIVATE src)
    target_link_libraries(pintest PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(pintest PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(pintest "pintest_resources"
        PREFIX "/"
        BASE resources
        FILES resources/icon.png
    )
endif()

# 工具栏各状态视觉验证
option(BUILD_BARSTATES "Build the toolbar state preview" OFF)

if(BUILD_BARSTATES)
    add_executable(barstates
        tools/barstates/main.cpp
    )
    target_include_directories(barstates PRIVATE src)
    target_link_libraries(barstates PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(barstates PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(barstates "barstates_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg   resources/icons/circle.svg
            resources/icons/minus.svg    resources/icons/arrow-up-right.svg
            resources/icons/spline.svg   resources/icons/pencil.svg
            resources/icons/highlighter.svg resources/icons/type.svg
            resources/icons/grid-3x3.svg resources/icons/circle-dashed.svg
            resources/icons/eraser.svg   resources/icons/undo-2.svg
            resources/icons/redo-2.svg   resources/icons/x.svg
        resources/icons/trash-2.svg
            resources/icons/copy.svg     resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
    )
endif()

# 工具栏悬停提示验证
option(BUILD_TOOLTIP_TEST "Build the toolbar tooltip preview" OFF)

if(BUILD_TOOLTIP_TEST)
    add_executable(tooltiptest
        tools/tooltiptest/main.cpp
    )
    target_include_directories(tooltiptest PRIVATE src)
    target_link_libraries(tooltiptest PRIVATE fcapture_core Qt6::Test)
    if(MSVC)
        target_compile_options(tooltiptest PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(tooltiptest "tooltip_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg
            resources/icons/circle.svg
            resources/icons/minus.svg
            resources/icons/arrow-up-right.svg
            resources/icons/spline.svg
            resources/icons/hash.svg
            resources/icons/lasso-select.svg
            resources/icons/scan-search.svg
            resources/icons/line-style.svg
            resources/icons/arrow-left-right.svg
            resources/icons/pencil.svg
            resources/icons/highlighter.svg
            resources/icons/type.svg
            resources/icons/grid-3x3.svg
            resources/icons/circle-dashed.svg
            resources/icons/eraser.svg
            resources/icons/undo-2.svg
            resources/icons/redo-2.svg
            resources/icons/x.svg
        resources/icons/trash-2.svg
            resources/icons/copy.svg
            resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
    )
endif()

# 文字输入框行为测试（需要 Qt6::Test）
find_package(Qt6 QUIET COMPONENTS Test)
option(BUILD_TEXT_INPUT_TEST "Build the text input behaviour test" OFF)

if(BUILD_TEXT_INPUT_TEST AND Qt6Test_FOUND)
    add_executable(textinputtest
        tools/textinputtest/main.cpp
    )
    target_include_directories(textinputtest PRIVATE src)
    target_link_libraries(textinputtest PRIVATE fcapture_core Qt6::Test)
    if(MSVC)
        target_compile_options(textinputtest PRIVATE /W4 /utf-8 /permissive-)
    endif()

endif()

# P3 Pro 标注离屏渲染验证
option(BUILD_P3_PREVIEW "Build the P3 pro-annotation render preview" OFF)

if(BUILD_P3_PREVIEW)
    add_executable(p3preview
        tools/p3preview/main.cpp
    )
    target_include_directories(p3preview PRIVATE src)
    target_link_libraries(p3preview PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(p3preview PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(p3preview "p3preview_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg
            resources/icons/circle.svg
            resources/icons/minus.svg
            resources/icons/arrow-up-right.svg
            resources/icons/spline.svg
            resources/icons/pencil.svg
            resources/icons/highlighter.svg
            resources/icons/type.svg
            resources/icons/grid-3x3.svg
            resources/icons/circle-dashed.svg
            resources/icons/eraser.svg
            resources/icons/undo-2.svg
            resources/icons/redo-2.svg
            resources/icons/x.svg
            resources/icons/trash-2.svg
            resources/icons/copy.svg
            resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
            resources/icons/hash.svg
            resources/icons/lasso-select.svg
            resources/icons/scan-search.svg
            resources/icons/line-style.svg
            resources/icons/arrow-left-right.svg
    )
endif()

# P4 Pro 截图特性验证（离屏）
option(BUILD_P4_TEST "Build the P4 pro-snip feature test" OFF)

if(BUILD_P4_TEST)
    add_executable(p4test
        tools/p4test/main.cpp
    )
    target_include_directories(p4test PRIVATE src)
    target_link_libraries(p4test PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(p4test PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(p4test "p4test_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg
            resources/icons/circle.svg
            resources/icons/minus.svg
            resources/icons/arrow-up-right.svg
            resources/icons/spline.svg
            resources/icons/pencil.svg
            resources/icons/highlighter.svg
            resources/icons/type.svg
            resources/icons/grid-3x3.svg
            resources/icons/circle-dashed.svg
            resources/icons/eraser.svg
            resources/icons/undo-2.svg
            resources/icons/redo-2.svg
            resources/icons/x.svg
            resources/icons/trash-2.svg
            resources/icons/copy.svg
            resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
            resources/icons/hash.svg
            resources/icons/lasso-select.svg
            resources/icons/scan-search.svg
            resources/icons/line-style.svg
            resources/icons/arrow-left-right.svg
    )
endif()

# 工具栏交互回归测试（索引越界 / 崩溃）
option(BUILD_BAR_TEST "Build the paint bar interaction regression test" OFF)

if(BUILD_BAR_TEST)
    add_executable(bartest
        tools/bartest/main.cpp
    )
    target_include_directories(bartest PRIVATE src)
    target_link_libraries(bartest PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(bartest PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(bartest "bartest_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg
            resources/icons/circle.svg
            resources/icons/minus.svg
            resources/icons/arrow-up-right.svg
            resources/icons/spline.svg
            resources/icons/pencil.svg
            resources/icons/highlighter.svg
            resources/icons/type.svg
            resources/icons/grid-3x3.svg
            resources/icons/circle-dashed.svg
            resources/icons/eraser.svg
            resources/icons/undo-2.svg
            resources/icons/redo-2.svg
            resources/icons/x.svg
            resources/icons/trash-2.svg
            resources/icons/copy.svg
            resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
            resources/icons/hash.svg
            resources/icons/lasso-select.svg
            resources/icons/scan-search.svg
            resources/icons/line-style.svg
            resources/icons/arrow-left-right.svg
    )
endif()

# 截图层右键语义回归测试
option(BUILD_OVERLAY_TEST "Build the snip overlay interaction regression test" OFF)

if(BUILD_OVERLAY_TEST)
    add_executable(overlaytest
        tools/overlaytest/main.cpp
    )
    target_include_directories(overlaytest PRIVATE src)
    target_link_libraries(overlaytest PRIVATE fcapture_core)
    if(MSVC)
        target_compile_options(overlaytest PRIVATE /W4 /utf-8 /permissive-)
    endif()
    qt_add_resources(overlaytest "overlaytest_icons"
        PREFIX "/icons"
        BASE resources/icons
        FILES
            resources/icons/square.svg
            resources/icons/circle.svg
            resources/icons/minus.svg
            resources/icons/arrow-up-right.svg
            resources/icons/spline.svg
            resources/icons/pencil.svg
            resources/icons/highlighter.svg
            resources/icons/type.svg
            resources/icons/grid-3x3.svg
            resources/icons/circle-dashed.svg
            resources/icons/eraser.svg
            resources/icons/undo-2.svg
            resources/icons/redo-2.svg
            resources/icons/x.svg
            resources/icons/trash-2.svg
            resources/icons/copy.svg
            resources/icons/save.svg
            resources/icons/pin.svg
            resources/icons/send.svg
            resources/icons/hash.svg
            resources/icons/lasso-select.svg
            resources/icons/scan-search.svg
            resources/icons/line-style.svg
            resources/icons/arrow-left-right.svg
    )
endif()


# P6 打磨项验证（便携路径 / ZIP 写入 / 崩溃标记 / 开机自启）
option(BUILD_P6_TEST "Build the P6 polish test" OFF)

if(BUILD_P6_TEST)
    add_executable(p6test
        tools/p6test/main.cpp
    )
    target_link_libraries(p6test PRIVATE fcapture_core)
endif()





# 临时：字体探针
option(BUILD_FONT_PROBE "font probe" OFF)
if(BUILD_FONT_PROBE)
    add_executable(fontprobe probe_tmp/fontprobe.cpp)
    target_link_libraries(fontprobe PRIVATE fcapture_core)
endif()
