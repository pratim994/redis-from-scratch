cmake_minimum_required(VERSION 3.16)
project(redis VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Compile options ─────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -O2
)

# ─── Include paths ────────────────────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR}/include)

# ─── Storage library ─────────────────────────────────────────────────────────
add_library(storage STATIC
    src/storage/avl.cpp
    src/storage/hashtable.cpp
    src/storage/zset.cpp
)

# ─── Utils library ────────────────────────────────────────────────────────────
add_library(utils STATIC
    src/utils/heap.cpp
)

# ─── Concurrency library ──────────────────────────────────────────────────────
add_library(concurrency STATIC
    src/concurrency/thread_pool.cpp
)
target_link_libraries(concurrency PUBLIC pthread)

# ─── Core (command logic) ─────────────────────────────────────────────────────
add_library(core STATIC
    src/core/commands.cpp
)
target_link_libraries(core PUBLIC storage utils concurrency)

# ─── Server executable ────────────────────────────────────────────────────────
add_executable(server src/network/server.cpp)
target_link_libraries(server PRIVATE core)

# ─── Client executable ────────────────────────────────────────────────────────
add_executable(client src/network/client.cpp)

# ─── Tests ────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent GTest from overriding our compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(tests
    tests/test_avl.cpp
    tests/test_hashtable.cpp
    tests/test_zset.cpp
    tests/test_thread_pool.cpp
)
target_link_libraries(tests
    PRIVATE
        storage
        concurrency
        core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(tests)
