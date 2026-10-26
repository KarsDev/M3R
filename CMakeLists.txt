cmake_minimum_required(VERSION 3.25)

project(M3R LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Raylib (GUI only)
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# shared engine headers and compile flags
set(ENGINE_HEADERS
        src/board.h
        src/attacks.h
        src/move.h
        src/MoveList.h
        src/movegen.h
        src/makemove.h
        src/legalmoves.h
        src/search.h
        src/transposition.h
        src/eval.h
)

add_library(m3r_flags INTERFACE)
if(MSVC)
    target_compile_options(m3r_flags INTERFACE /O2 /DNDEBUG)
else()
    target_compile_options(m3r_flags INTERFACE -O3 -DNDEBUG)

    # Enable native CPU optimizations on GCC/Clang.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(m3r_flags INTERFACE -march=native)
    endif()
endif()

# console version (play, --test, --bench)
add_executable(M3R src/main.cpp ${ENGINE_HEADERS}
        src/ponder.h)
target_link_libraries(M3R PRIVATE m3r_flags)

# GUI version
add_executable(M3R_GUI src/maingui.cpp ${ENGINE_HEADERS})
target_link_libraries(M3R_GUI PRIVATE m3r_flags raylib)

add_custom_command(TARGET M3R_GUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/DejaVuSans.ttf
        $<TARGET_FILE_DIR:M3R_GUI>/DejaVuSans.ttf)