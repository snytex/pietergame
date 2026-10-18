cmake_minimum_required(VERSION 3.20)
project(PieterGame3 VERSION 3.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# raylib is the platform layer (window, input, GL, audio decoding).
# Use the system package when available, otherwise download it.
# raylib >= 6.0 is needed: its fullscreen keeps the monitor's video mode.
find_package(raylib 6.0 QUIET)
if(NOT raylib_FOUND)
  include(FetchContent)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(raylib)
endif()

file(GLOB_RECURSE PG_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.c)
set_source_files_properties(src/external/pl_mpeg.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
add_executable(pietergame3 ${PG_SOURCES})
target_include_directories(pietergame3 PRIVATE src)
target_link_libraries(pietergame3 PRIVATE raylib)
target_compile_definitions(pietergame3 PRIVATE PG_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
target_compile_definitions(pietergame3 PRIVATE PG_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(pietergame3 PRIVATE /W4)
else()
  target_compile_options(pietergame3 PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  if(UNIX AND NOT APPLE)
    target_link_libraries(pietergame3 PRIVATE m pthread dl)
  endif()
  if(MINGW)
    # self-contained .exe: no MinGW runtime DLLs needed, no console window
    target_link_options(pietergame3 PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
  endif()
endif()

# Copy assets next to the binary so the build folder is self-contained.
add_custom_command(TARGET pietergame3 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:pietergame3>/assets)

install(TARGETS pietergame3 RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
