cmake_minimum_required(VERSION 3.16)
project(ohdsp_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ohdsp STATIC src/processor.cpp src/fit.cpp)
target_include_directories(ohdsp PUBLIC include)
target_compile_options(ohdsp PRIVATE -Wall -Wextra -Werror -fno-fast-math -fno-finite-math-only)

add_executable(ohdsp_tests tests/test_main.cpp tests/test_v21.cpp)
target_link_libraries(ohdsp_tests PRIVATE ohdsp)
target_compile_options(ohdsp_tests PRIVATE -Wall -Wextra -Werror -fno-fast-math -fno-finite-math-only)
find_package(Threads REQUIRED)
target_link_libraries(ohdsp_tests PRIVATE Threads::Threads)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(ohdsp PRIVATE NDEBUG)
  target_compile_definitions(ohdsp_tests PRIVATE NDEBUG)
endif()
