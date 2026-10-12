find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE drawcore Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)

qt_add_executable(tst_document tst_document.cpp)
target_link_libraries(tst_document PRIVATE drawcore Qt6::Test)
add_test(NAME tst_document COMMAND tst_document)

qt_add_executable(tst_recovery tst_recovery.cpp)
target_link_libraries(tst_recovery PRIVATE drawcore Qt6::Test)
add_test(NAME tst_recovery COMMAND tst_recovery)

# Drives the real CanvasItem in a window with synthetic mouse, pen and touch events.
qt_add_executable(tst_canvas tst_canvas.cpp ../src/app/CanvasItem.cpp ../src/app/CanvasItem.h)
target_include_directories(tst_canvas PRIVATE ../src/app)
target_link_libraries(tst_canvas PRIVATE drawcore Qt6::Quick Qt6::Test)
add_test(NAME tst_canvas COMMAND tst_canvas)

qt_add_executable(bench_core bench_core.cpp)
target_link_libraries(bench_core PRIVATE drawcore)

if(WIN32)
    # Let ctest find the Qt and MinGW DLLs without a deployment step.
    get_filename_component(_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(tst_core tst_document tst_recovery tst_canvas PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_cxx_bin}")
endif()
