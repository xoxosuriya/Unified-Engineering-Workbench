cmake_minimum_required(VERSION 3.16)
project(UnifiedEngineeringWorkbench VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization and warning flags
if(MSVC)
    add_compile_options(/W4 /O2 /permissive-)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -O3 -pthread)
    # Statically link runtime for zero-dependency portability on Windows
    if(WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    endif()
endif()

# Programmatic compiler and optimization metadata
set(COMPILER_INFO_STR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
set(OPT_FLAGS_STR "${CMAKE_CXX_FLAGS}")
if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)
    if(CMAKE_CXX_FLAGS_${UPPER_BUILD_TYPE})
        set(OPT_FLAGS_STR "${OPT_FLAGS_STR} ${CMAKE_CXX_FLAGS_${UPPER_BUILD_TYPE}}")
    endif()
endif()
if(NOT MSVC)
    set(OPT_FLAGS_STR "${OPT_FLAGS_STR} -O3")
endif()
string(STRIP "${OPT_FLAGS_STR}" OPT_FLAGS_STR)

add_compile_definitions(
    WORKBENCH_COMPILER_INFO="${COMPILER_INFO_STR}"
    WORKBENCH_OPT_FLAGS="${OPT_FLAGS_STR}"
)

# Include directories
include_directories(
    src/backend/include
)

# Core Backend Library
add_library(workbench_core STATIC
    src/backend/src/stl_parser.cpp
    src/backend/src/ansys_parser.cpp
    src/backend/src/thermal_fea_1d.cpp
    src/backend/src/thread_pool.cpp
    src/backend/src/multicore_bench.cpp
    src/backend/src/embedded_hardware.cpp
    src/backend/src/backprop_engine.cpp
    src/backend/src/http_server.cpp
)

if(WIN32)
    target_link_libraries(workbench_core PUBLIC ws2_32)
endif()

# CLI and Headless Server Executable
add_executable(workbench_backend
    src/backend/src/main.cpp
)
target_link_libraries(workbench_backend PRIVATE workbench_core)

# Standalone Launcher for Windows (Double-clickable Launch.exe)
if(WIN32)
    add_executable(Launch WIN32
        src/backend/launcher/launcher_win.cpp
    )
    target_link_libraries(Launch PRIVATE workbench_core shell32 user32)
endif()

# Automated Test Suite
enable_testing()
add_executable(workbench_tests
    tests/test_runner.cpp
)
target_link_libraries(workbench_tests PRIVATE workbench_core)
add_test(NAME AllWorkbenchTests COMMAND workbench_tests)

# Install / Packaging Configuration
install(TARGETS Launch workbench_backend workbench_tests RUNTIME DESTINATION .)
install(DIRECTORY src/frontend DESTINATION src)
install(DIRECTORY data DESTINATION .)
install(DIRECTORY docs DESTINATION .)
install(DIRECTORY results DESTINATION .)
install(FILES README.md CMakeLists.txt DESTINATION .)
