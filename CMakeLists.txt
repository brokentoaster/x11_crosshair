cmake_minimum_required(VERSION 3.13)
project(x11_crosshair VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find X11
find_package(X11 REQUIRED)

# Use pkg-config to find Xfixes
find_package(PkgConfig REQUIRED)
pkg_check_modules(XFIXES REQUIRED xfixes)
pkg_check_modules(XI REQUIRED xi)

# Single executable: overlay with runtime-selectable modes
add_executable(x11_crosshair main.cpp)



# Include directories from found packages
target_include_directories(x11_crosshair PRIVATE
        ${X11_INCLUDE_DIR}
        ${XFIXES_INCLUDE_DIRS}
        ${XI_INCLUDE_DIRS}
)


# Link required libraries
target_link_libraries(x11_crosshair PRIVATE
        ${X11_LIBRARIES}
        ${XFIXES_LIBRARIES}
        ${XI_LIBRARIES}
)




# Add some useful compiler flags
target_compile_options(x11_crosshair PRIVATE -Wall -Wextra -Wpedantic)

option(BUILD_TESTS "Build unit tests for the X-free logic" ON)
if(BUILD_TESTS)
  enable_testing()
  add_executable(x11_crosshair_tests tests/test_logic.cpp)
  target_compile_options(x11_crosshair_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME logic COMMAND x11_crosshair_tests)
endif()

# Aggressive GCC-only optimizations for Release builds
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(x11_crosshair PRIVATE
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-O3>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-march=native>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-funroll-loops>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-ffast-math>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-fomit-frame-pointer>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-fstrict-aliasing>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-flto>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-pipe>
    )

    target_link_options(x11_crosshair PRIVATE
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-flto>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-Wl,-O1>
            $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-Wl,--as-needed>
    )
endif()

# If pkg-config reported library dirs, ensure the linker can find them
if(XFIXES_LIBRARY_DIRS)
    link_directories(${XFIXES_LIBRARY_DIRS})
endif()
if(XI_LIBRARY_DIRS)
    link_directories(${XI_LIBRARY_DIRS})
endif()
