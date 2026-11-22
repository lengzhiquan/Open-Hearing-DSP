cmake_minimum_required(VERSION 3.22.1)
project(openhearing)

set(OBOE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/oboe)
set(OHDSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../native/ohdsp)

add_subdirectory(${OBOE_DIR} ${CMAKE_BINARY_DIR}/oboe)

add_library(openhearing SHARED
        jni_bridge.cpp
        engine.cpp
        ${OHDSP_DIR}/src/processor.cpp
        ${OHDSP_DIR}/src/fit.cpp)

target_include_directories(openhearing PRIVATE
        ${OHDSP_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(openhearing PRIVATE cxx_std_17)
# -O2 even for the Debug APK so the audio callback is not built at -O0.
# fast-math is off so NaN/Inf checks stay real.
target_compile_options(openhearing PRIVATE
        -O2
        -fno-fast-math
        -fno-finite-math-only
        -Wall
        -Wextra
        -Wno-unused-parameter)

target_link_libraries(openhearing PRIVATE oboe log atomic)
target_link_options(openhearing PRIVATE "-Wl,-z,max-page-size=16384")
