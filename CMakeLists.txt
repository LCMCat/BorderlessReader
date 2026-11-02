cmake_minimum_required(VERSION 3.21)

project(BorderlessReader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(BorderlessReader WIN32
    main.cpp

    core/IToolPlugin.h
    core/RegionSelector.h
    core/RegionSelector.cpp
    core/Utils/ScreenUtils.h
    core/Utils/ScreenUtils.cpp
    core/Utils/CaptureUtils.h
    core/Utils/CaptureUtils.cpp

    shell/PluginManager.h
    shell/PluginManager.cpp
    shell/FloatBar.h
    shell/FloatBar.cpp

    plugins/DemoPlugin.h
    plugins/DemoPlugin.cpp
)

target_include_directories(BorderlessReader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BorderlessReader PRIVATE
    Qt6::Widgets
)

# 源码含有中文字符串，MSVC 必须显式按 UTF-8 编译。
if(MSVC)
    target_compile_options(BorderlessReader PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(BorderlessReader PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(BorderlessReader PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "BorderlessReader"
)
