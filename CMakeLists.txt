cmake_minimum_required(VERSION 3.21)

project(BorderlessReader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# OCR 后端开关。Tesseract 用 third_party/tesseract 里的运行库（运行时动态加载），
# Windows.Media.Ocr 作为运行库缺失时的兜底；两者都只在 Windows + MSVC 下编译。
option(PAPERREADER_ENABLE_TESSERACT
    "使用 third_party/tesseract 作为首选 OCR 后端（需要 Windows + MSVC）" ON)
option(PAPERREADER_ENABLE_WINOCR
    "使用 Windows.Media.Ocr 作为兜底 OCR 后端（需要 Windows + MSVC）" ON)

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

    core/Ocr/OcrBackend.h
    core/Ocr/OcrBackends.h
    core/Ocr/PlatformOcrBackend.cpp
    core/Ocr/TextRecognizer.h
    core/Ocr/TextRecognizer.cpp

    shell/PluginManager.h
    shell/PluginManager.cpp
    shell/FloatBar.h
    shell/FloatBar.cpp

    plugins/DemoPlugin.h
    plugins/DemoPlugin.cpp

    plugins/WordLookup/WordLookupPlugin.h
    plugins/WordLookup/WordLookupPlugin.cpp
    plugins/WordLookup/LookupDialog.h
    plugins/WordLookup/LookupDialog.cpp
    plugins/WordLookup/WordPicker.h
    plugins/WordLookup/WordPicker.cpp
    plugins/WordLookup/lookup/WordEntry.h
    plugins/WordLookup/lookup/LookupResult.h
    plugins/WordLookup/lookup/LookupProvider.h
    plugins/WordLookup/lookup/WordForms.h
    plugins/WordLookup/lookup/WordForms.cpp
    plugins/WordLookup/lookup/LocalDictionary.h
    plugins/WordLookup/lookup/LocalDictionary.cpp
    plugins/WordLookup/lookup/LookupService.h
    plugins/WordLookup/lookup/LookupService.cpp
)

target_include_directories(BorderlessReader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BorderlessReader PRIVATE
    Qt6::Widgets
)

# OCR 后端：优先 Tesseract（贡献者的运行库，识别小字号明显更好），
# 运行库缺失时退回 Windows.Media.Ocr。两个开关都关掉时没有任何 OCR 后端。
set(PAPERREADER_WINOCR_ENABLED OFF)
if(WIN32 AND MSVC AND PAPERREADER_ENABLE_WINOCR)
    set(PAPERREADER_WINOCR_ENABLED ON)
endif()

set(PAPERREADER_TESSERACT_ENABLED OFF)
if(WIN32 AND MSVC AND PAPERREADER_ENABLE_TESSERACT)
    set(PAPERREADER_TESSERACT_ENABLED ON)
endif()

if(PAPERREADER_TESSERACT_ENABLED)
    target_sources(BorderlessReader PRIVATE core/Ocr/TesseractBackend.cpp)
    target_compile_definitions(BorderlessReader PRIVATE PAPERREADER_HAS_TESSERACT=1)
    message(STATUS "BorderlessReader: OCR backend = Tesseract (runtime: third_party/tesseract)")
else()
    message(STATUS "BorderlessReader: OCR backend = Tesseract disabled")
endif()

if(PAPERREADER_WINOCR_ENABLED)
    target_sources(BorderlessReader PRIVATE core/Ocr/WindowsOcrBackend.cpp)
    target_compile_definitions(BorderlessReader PRIVATE PAPERREADER_HAS_WINOCR=1)
    target_link_libraries(BorderlessReader PRIVATE windowsapp)
    message(STATUS "BorderlessReader: OCR fallback = Windows.Media.Ocr")
else()
    message(STATUS "BorderlessReader: OCR fallback = Windows.Media.Ocr disabled")
endif()

# 词库随仓库分发。运行时路径固定为 <exe>/dict，构建后把数据放到那里，
# 不再做“向上找几级目录”或读环境变量。
set(PAPERREADER_DICT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dict")
add_custom_command(TARGET BorderlessReader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:BorderlessReader>/dict"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAPERREADER_DICT_DIR}/ecdict_full.tsv"
            "${PAPERREADER_DICT_DIR}/forms_full.tsv"
            "$<TARGET_FILE_DIR:BorderlessReader>/dict"
    COMMENT "Copying dictionary data next to the executable"
)

# Tesseract 运行库同样固定放在 <exe>/third_party/tesseract；只复制变化的文件，
# 首次构建会多花一两秒，之后不再重复拷贝。
if(PAPERREADER_TESSERACT_ENABLED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tesseract")
    add_custom_command(TARGET BorderlessReader POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/third_party/tesseract"
                "-DDST=$<TARGET_FILE_DIR:BorderlessReader>/third_party/tesseract"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
        COMMENT "Copying Tesseract runtime next to the executable"
    )
endif()

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
