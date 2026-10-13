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
    src/core/AndroidDocumentProvider.cpp
    src/core/BrushPresetFile.cpp src/core/BrushPresetFile.h
    src/core/BrushPresetStore.cpp src/core/BrushPresetStore.h
    src/core/BrushPresets.cpp src/core/BrushPresets.h
    src/core/BrushSettings.cpp src/core/BrushSettings.h
    src/core/BrushDynamics.cpp src/core/BrushDynamics.h
    src/core/BrushProperties.cpp src/core/BrushProperties.h
    src/core/BrushTips.cpp src/core/BrushTips.h
    src/core/DocumentData.cpp src/core/DocumentData.h
    src/core/DocumentProvider.cpp src/core/DocumentProvider.h
    src/core/LayeredDocument.cpp src/core/LayeredDocument.h
    src/core/MemoryBudget.cpp src/core/MemoryBudget.h
    src/core/Pressure.h
    src/core/ProjectFile.cpp src/core/ProjectFile.h
    src/core/ProjectIo.cpp src/core/ProjectIo.h
    src/core/RasterDocument.cpp src/core/RasterDocument.h
    src/core/RecoveryFile.cpp src/core/RecoveryFile.h
    src/core/RecoveryManager.cpp src/core/RecoveryManager.h
    src/core/SafeSave.cpp src/core/SafeSave.h
    src/core/Selection.cpp src/core/Selection.h
    src/core/StrokeEngine.cpp src/core/StrokeEngine.h
    src/core/StrokeContext.h src/core/BrushRaster.h
    src/core/StrokeCurve.cpp src/core/StrokeCurve.h
    src/core/PigmentMix.cpp src/core/PigmentMix.h
    src/core/StrokeDeposit.cpp src/core/StrokeDeposit.h
    src/core/CoverageDeposit.cpp src/core/CoverageDeposit.h
    src/core/CarriedDeposit.cpp src/core/CarriedDeposit.h
    src/core/DryDeposit.cpp src/core/DryDeposit.h
    src/core/BristleDeposit.cpp src/core/BristleDeposit.h
    src/core/StrokeSmoother.cpp src/core/StrokeSmoother.h
    src/core/StrokeSpacer.cpp src/core/StrokeSpacer.h
    src/core/TiledImage.cpp src/core/TiledImage.h
    src/core/TileSnapshot.cpp src/core/TileSnapshot.h
    src/core/TransformApply.cpp src/core/TransformApply.h
    src/core/TransformSession.cpp src/core/TransformSession.h
    src/core/UndoHistory.cpp src/core/UndoHistory.h
    src/core/ViewPyramid.cpp src/core/ViewPyramid.h
    src/core/ViewRenderer.cpp src/core/ViewRenderer.h
    src/core/ViewTransform.cpp src/core/ViewTransform.h
)
target_include_directories(drawcore PUBLIC src/core)
target_link_libraries(drawcore PUBLIC Qt6::Gui)

# The QML user interface as a static QML module, so the app and the UI test load the same Main.qml.
qt_add_library(drawapp_ui STATIC)
qt_add_qml_module(drawapp_ui
    URI DrawApp
    VERSION 1.0
    QML_FILES src/app/Main.qml src/app/BrushPanel.qml src/app/BrushStudioPage.qml src/app/CurveEditor.qml
    SOURCES src/app/CanvasItem.cpp src/app/CanvasItem.h src/app/CanvasItemSelection.cpp
            src/app/BrushLibrary.cpp src/app/BrushLibrary.h
            src/app/BrushStudio.cpp src/app/BrushStudio.h
            src/app/BrushScratchpad.cpp src/app/BrushScratchpad.h
            src/app/BrushThumbnails.cpp src/app/BrushThumbnails.h
)
# The generated QML type registration includes <CanvasItem.h>.
target_include_directories(drawapp_ui PUBLIC src/app)
target_link_libraries(drawapp_ui PUBLIC drawcore Qt6::Quick Qt6::QuickControls2)

qt_add_executable(drawapp src/app/main.cpp)
target_link_libraries(drawapp PRIVATE drawapp_ui drawapp_uiplugin)
if(WIN32)
    target_link_libraries(drawapp PRIVATE psapi) # memory figures for DRAWAPP_STARTUP_PROBE
endif()
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
