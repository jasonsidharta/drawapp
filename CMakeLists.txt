cmake_minimum_required(VERSION 3.22)

project(DrawApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

option(DRAWAPP_BUILD_TESTS "Build unit tests and benchmarks" ON)

# Document model and raster operations: QtGui only, no Qt Quick, so they can be tested headless.
add_library(drawcore STATIC
    src/core/Pressure.h
    src/core/ProjectFile.cpp src/core/ProjectFile.h
    src/core/RasterDocument.cpp src/core/RasterDocument.h
    src/core/StrokeSpacer.cpp src/core/StrokeSpacer.h
    src/core/TileSnapshot.cpp src/core/TileSnapshot.h
    src/core/UndoHistory.cpp src/core/UndoHistory.h
    src/core/ViewRenderer.cpp src/core/ViewRenderer.h
    src/core/ViewTransform.cpp src/core/ViewTransform.h
)
target_include_directories(drawcore PUBLIC src/core)
target_link_libraries(drawcore PUBLIC Qt6::Gui)

qt_add_executable(drawapp src/app/main.cpp)
qt_add_qml_module(drawapp
    URI DrawApp
    VERSION 1.0
    QML_FILES src/app/Main.qml
    SOURCES src/app/CanvasItem.cpp src/app/CanvasItem.h
)
# The generated QML type registration includes <CanvasItem.h>.
target_include_directories(drawapp PRIVATE src/app)
target_link_libraries(drawapp PRIVATE drawcore Qt6::Quick Qt6::QuickControls2)
set_target_properties(drawapp PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.drawapp.prototype
    QT_ANDROID_PACKAGE_NAME org.drawapp.prototype
    QT_ANDROID_TARGET_SDK_VERSION 36
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
    QT_ANDROID_VERSION_CODE 1
)

install(TARGETS drawapp
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(NOT ANDROID AND NOT IOS)
    qt_generate_deploy_qml_app_script(TARGET drawapp OUTPUT_SCRIPT deploy_script)
    install(SCRIPT ${deploy_script})
endif()

if(DRAWAPP_BUILD_TESTS AND NOT ANDROID AND NOT IOS)
    enable_testing()
    add_subdirectory(tests)
endif()
