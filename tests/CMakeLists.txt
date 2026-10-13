find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE drawcore Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)

qt_add_executable(tst_brush tst_brush.cpp)
target_link_libraries(tst_brush PRIVATE drawcore Qt6::Test)
add_test(NAME tst_brush COMMAND tst_brush)

qt_add_executable(tst_brushengine tst_brushengine.cpp)
target_link_libraries(tst_brushengine PRIVATE drawcore Qt6::Test)
target_compile_definitions(tst_brushengine PRIVATE DRAWAPP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME tst_brushengine COMMAND tst_brushengine)

qt_add_executable(tst_brushmedia tst_brushmedia.cpp)
target_link_libraries(tst_brushmedia PRIVATE drawcore Qt6::Test)
target_compile_definitions(tst_brushmedia PRIVATE DRAWAPP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME tst_brushmedia COMMAND tst_brushmedia)

qt_add_executable(tst_fileformat tst_fileformat.cpp)
target_link_libraries(tst_fileformat PRIVATE drawcore Qt6::Test)
target_compile_definitions(tst_fileformat PRIVATE DRAWAPP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME tst_fileformat COMMAND tst_fileformat)

qt_add_executable(tst_tiles tst_tiles.cpp)
target_link_libraries(tst_tiles PRIVATE drawcore Qt6::Test)
add_test(NAME tst_tiles COMMAND tst_tiles)

qt_add_executable(tst_selection tst_selection.cpp)
target_link_libraries(tst_selection PRIVATE drawcore Qt6::Test)
add_test(NAME tst_selection COMMAND tst_selection)

qt_add_executable(tst_document tst_document.cpp)
target_link_libraries(tst_document PRIVATE drawcore Qt6::Test)
add_test(NAME tst_document COMMAND tst_document)

qt_add_executable(tst_recovery tst_recovery.cpp)
target_link_libraries(tst_recovery PRIVATE drawcore Qt6::Test)
add_test(NAME tst_recovery COMMAND tst_recovery)

# Drives the real CanvasItem in a window with synthetic mouse, pen and touch events.
set(CANVAS_SOURCES
    ../src/app/CanvasItem.cpp ../src/app/CanvasItem.h ../src/app/CanvasItemSelection.cpp
    ../src/app/BrushLibrary.cpp ../src/app/BrushLibrary.h
    ../src/app/BrushStudio.cpp ../src/app/BrushStudio.h
    ../src/app/BrushScratchpad.cpp ../src/app/BrushScratchpad.h
    ../src/app/BrushThumbnails.cpp ../src/app/BrushThumbnails.h)
qt_add_executable(tst_canvas tst_canvas.cpp ${CANVAS_SOURCES})
target_include_directories(tst_canvas PRIVATE ../src/app)
target_link_libraries(tst_canvas PRIVATE drawcore Qt6::Quick Qt6::Test)
add_test(NAME tst_canvas COMMAND tst_canvas)

# Loads the real Main.qml (static QML module) and drives the toolbar and layer panel.
qt_add_executable(tst_ui tst_ui.cpp)
target_link_libraries(tst_ui PRIVATE drawapp_ui drawapp_uiplugin Qt6::Test)
target_compile_definitions(tst_ui PRIVATE DRAWAPP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME tst_ui COMMAND tst_ui)

# Real CanvasItem under synthetic strokes with autosave and an async save (not part of ctest).
qt_add_executable(bench_canvas bench_canvas.cpp ${CANVAS_SOURCES})
target_include_directories(bench_canvas PRIVATE ../src/app)
target_link_libraries(bench_canvas PRIVATE drawcore Qt6::Quick Qt6::Test)

# Renders every built-in preset on the standard strokes (not part of ctest): brush_sheet out.png
qt_add_executable(brush_sheet brush_sheet.cpp)
target_link_libraries(brush_sheet PRIVATE drawcore)

# Milestone 10 tile storage sizes (not part of ctest).
qt_add_executable(bench_tiles bench_tiles.cpp)
target_link_libraries(bench_tiles PRIVATE drawcore)

# Milestone 9 workloads, brush engines, long strokes and multi-layer transforms (not part of ctest).
qt_add_executable(bench_m9 bench_m9.cpp)
target_link_libraries(bench_m9 PRIVATE drawcore)
if(WIN32)
    target_link_libraries(bench_m9 PRIVATE psapi)
endif()

qt_add_executable(bench_core bench_core.cpp)
target_link_libraries(bench_core PRIVATE drawcore)
if(WIN32)
    target_link_libraries(bench_core PRIVATE psapi)
endif()

if(WIN32)
    # Let ctest find the Qt and MinGW DLLs without a deployment step.
    get_filename_component(_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(tst_core tst_brush tst_brushengine tst_tiles tst_fileformat tst_selection tst_document tst_recovery tst_canvas tst_ui PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_cxx_bin}")
    # A low floor for the device-memory check, so a busy development machine does not make
    # unrelated tests fail; tests of the floor set it themselves.
    set_property(TEST tst_core tst_brush tst_brushengine tst_tiles tst_fileformat tst_selection tst_document tst_recovery tst_canvas tst_ui
                 APPEND PROPERTY ENVIRONMENT_MODIFICATION "DRAWAPP_MEMORY_FLOOR_MB=set:64")
    # Window tests render offscreen under ctest so they do not take over the desktop.
    set_property(TEST tst_canvas tst_ui APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "QT_QPA_PLATFORM=set:offscreen;QT_QPA_FONTDIR=set:C:/Windows/Fonts")
endif()
