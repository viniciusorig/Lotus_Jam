find_package(GTest REQUIRED)

file(GLOB TREINO_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
list(FILTER TREINO_TEST_SOURCES EXCLUDE REGEX "/ui_[^/]*_test\\.cpp$")

add_executable(treino_tests ${TREINO_TEST_SOURCES})
target_link_libraries(treino_tests PRIVATE treino_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(treino_tests DISCOVERY_MODE PRE_TEST)

# Allocation test lives in its own executable (it replaces global operator new).
add_executable(treino_rt_tests rt/click_scheduler_rt_test.cpp)
target_link_libraries(treino_rt_tests PRIVATE treino_core GTest::gtest_main)
gtest_discover_tests(treino_rt_tests DISCOVERY_MODE PRE_TEST)

# Qt UI smoke tests: own executable (own main with a QApplication), always offscreen.
add_executable(treino_ui_tests ui_smoke_test.cpp)
target_link_libraries(treino_ui_tests PRIVATE lotus_ui GTest::gtest)
target_compile_definitions(treino_ui_tests PRIVATE UI_SMOKE_PNG="${CMAKE_BINARY_DIR}/ui_smoke.png"
  UI_SMOKE_RUNNING_PNG="${CMAKE_BINARY_DIR}/ui_smoke_running.png")
gtest_discover_tests(treino_ui_tests DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS ui)
