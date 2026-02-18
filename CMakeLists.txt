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

# Create executable from main.cpp
add_executable(x11_crosshair main_sel.cpp)



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

add_executable(x11_crosshair2 main2.cpp CrosshairOverlay.cpp)
target_include_directories(x11_crosshair2 PRIVATE
        ${X11_INCLUDE_DIR}
        ${XFIXES_INCLUDE_DIRS}
        ${XI_INCLUDE_DIRS}
)
target_link_libraries(x11_crosshair2 PRIVATE
        ${X11_LIBRARIES}
        ${XFIXES_LIBRARIES}
        ${XI_LIBRARIES}
)
target_compile_options(x11_crosshair2 PRIVATE -O3 -march=native -mtune=native -fno-exceptions -fno-rtti -flto -pipe -Wall -Wextra -Wpedantic)
target_link_options(x11_crosshair2 PRIVATE -flto -Wl,-O3 -Wl,--as-needed)

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
