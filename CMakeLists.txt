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

# =========================================================================
# Dependency Management: Eigen 3.4.0 (Header-Only Linear Algebra Library)
# =========================================================================
include(FetchContent)

# Documented offline fallback options:
# 1. User-specified CMake cache variable: -DEIGEN_SOURCE_DIR=<path>
# 2. Local vendor directory: ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen-3.4.0
# 3. Upstream Git repository tagged release 3.4.0 (commit 3147391d946bb4b6c68edd901f2add6ac1f31f8c)

set(EIGEN_SOURCE_DIR "" CACHE PATH "Path to local Eigen source directory (offline fallback)")

if(EIGEN_SOURCE_DIR AND EXISTS "${EIGEN_SOURCE_DIR}/Eigen/Dense")
    message(STATUS "Eigen 3.4.0: Using offline path via EIGEN_SOURCE_DIR: ${EIGEN_SOURCE_DIR}")
    FetchContent_Declare(
        Eigen3
        SOURCE_DIR "${EIGEN_SOURCE_DIR}"
    )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen-3.4.0/Eigen/Dense")
    message(STATUS "Eigen 3.4.0: Using offline bundled path: ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen-3.4.0")
    FetchContent_Declare(
        Eigen3
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen-3.4.0"
    )
else()
    message(STATUS "Eigen 3.4.0: Fetching upstream from Git tag 3.4.0...")
    FetchContent_Declare(
        Eigen3
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3.4.0
        GIT_SHALLOW TRUE
    )
endif()

# Header-only library: disable upstream tests, documentation and pkg-config
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Eigen3)

# Core Backend Sources
set(WORKBENCH_CORE_SOURCES
    src/backend/src/stl_parser.cpp
    src/backend/src/ansys_parser.cpp
    src/backend/src/thermal_fea_1d.cpp
    src/backend/src/thread_pool.cpp
    src/backend/src/multicore_bench.cpp
    src/backend/src/embedded_hardware.cpp
    src/backend/src/backprop_engine.cpp
    src/backend/src/http_server.cpp
    src/backend/src/sparse_matrix.cpp
    src/backend/src/thermal_solvers_3d.cpp
    src/backend/src/thermal_geometry_3d.cpp
    src/backend/src/comparative_benchmark_3d.cpp
)

find_package(Threads REQUIRED)

# Core Backend Library (Production: Zero Test Hooks)
add_library(workbench_core STATIC
    ${WORKBENCH_CORE_SOURCES}
)

target_link_libraries(workbench_core PUBLIC Eigen3::Eigen Threads::Threads)

# Treat Eigen includes as SYSTEM headers to prevent third-party compiler warnings
get_target_property(EIGEN_INC Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
if(EIGEN_INC)
    target_include_directories(workbench_core SYSTEM PUBLIC ${EIGEN_INC})
endif()

if(WIN32)
    target_link_libraries(workbench_core PUBLIC ws2_32 psapi)
endif()

# Core Backend Library (Test-Enabled: Controlled Test Hooks for Concurrency Testing)
add_library(workbench_core_testing STATIC
    ${WORKBENCH_CORE_SOURCES}
)
target_compile_definitions(workbench_core_testing PUBLIC WORKBENCH_ENABLE_TEST_HOOKS=1)
target_link_libraries(workbench_core_testing PUBLIC Eigen3::Eigen Threads::Threads)
if(EIGEN_INC)
    target_include_directories(workbench_core_testing SYSTEM PUBLIC ${EIGEN_INC})
endif()
if(WIN32)
    target_link_libraries(workbench_core_testing PUBLIC ws2_32 psapi)
endif()

# CLI and Headless Server Executable (Production)
add_executable(workbench_backend
    src/backend/src/main.cpp
)
target_link_libraries(workbench_backend PRIVATE workbench_core)

# Standalone Launcher for Windows (Double-clickable Launch.exe) (Production)
if(WIN32)
    add_executable(Launch WIN32
        src/backend/launcher/launcher_win.cpp
    )
    target_link_libraries(Launch PRIVATE workbench_core shell32 user32)
endif()

# Automated Test Suite (Test-Enabled)
enable_testing()
add_executable(workbench_tests
    tests/test_runner.cpp
)
target_compile_definitions(workbench_tests PRIVATE WORKBENCH_ENABLE_TEST_HOOKS=1)
target_link_libraries(workbench_tests PRIVATE workbench_core_testing)
add_test(NAME AllWorkbenchTests COMMAND workbench_tests)

# Install / Packaging Configuration
if(WIN32)
    install(TARGETS Launch workbench_backend workbench_tests RUNTIME DESTINATION .)
else()
    install(TARGETS workbench_backend workbench_tests RUNTIME DESTINATION .)
endif()
install(DIRECTORY src/frontend DESTINATION src)
install(DIRECTORY data DESTINATION .)
install(DIRECTORY docs DESTINATION .)
install(DIRECTORY results DESTINATION .)
install(FILES README.md CMakeLists.txt DESTINATION .)
