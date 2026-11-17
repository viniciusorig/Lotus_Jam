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

# PipeWire (audio backend). Headers come from the system; nothing is downloaded.
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
target_link_libraries(treino_core PUBLIC PkgConfig::PIPEWIRE)

# Lotus Jam front-end (Qt 6 Widgets). Kept out of treino_core, which stays Qt-free.
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

file(GLOB LOTUS_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/ui/*.cpp ${CMAKE_SOURCE_DIR}/src/ui/*.h)
add_library(lotus_ui STATIC ${LOTUS_UI_SOURCES} ${CMAKE_SOURCE_DIR}/resources.qrc)
target_include_directories(lotus_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lotus_ui PUBLIC treino_core Qt6::Widgets Qt6::Svg)
# GridScorer::Summary has a member named `slots`: no Qt keyword macros (use Q_SIGNALS/Q_SLOTS/Q_EMIT).
target_compile_definitions(lotus_ui PUBLIC QT_NO_KEYWORDS)

add_executable(lotus-jam ${CMAKE_SOURCE_DIR}/src/main.cpp)
target_link_libraries(lotus-jam PRIVATE lotus_ui)

enable_testing()
add_subdirectory(tests)
