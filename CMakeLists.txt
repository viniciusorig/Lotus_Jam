cmake_minimum_required(VERSION 3.20)
project(treino LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TREINO_TSAN "Build with ThreadSanitizer" OFF)

add_compile_options(-Wall -Wextra)
if(TREINO_TSAN)
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
endif()

# UI sources (src/ui) are intentionally excluded from the core library.
file(GLOB_RECURSE TREINO_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/core/*.cpp
  ${CMAKE_SOURCE_DIR}/src/dsp/*.cpp
  ${CMAKE_SOURCE_DIR}/src/score/*.cpp
  ${CMAKE_SOURCE_DIR}/src/audio/*.cpp
  ${CMAKE_SOURCE_DIR}/src/app/*.cpp)

add_library(treino_core STATIC ${TREINO_CORE_SOURCES})
target_include_directories(treino_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(treino_core PUBLIC cxx_std_20)

enable_testing()
add_subdirectory(tests)
