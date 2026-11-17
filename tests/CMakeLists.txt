find_package(GTest REQUIRED)

file(GLOB TREINO_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)

add_executable(treino_tests ${TREINO_TEST_SOURCES})
target_link_libraries(treino_tests PRIVATE treino_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(treino_tests DISCOVERY_MODE PRE_TEST)

# Allocation test lives in its own executable (it replaces global operator new).
add_executable(treino_rt_tests rt/click_scheduler_rt_test.cpp)
target_link_libraries(treino_rt_tests PRIVATE treino_core GTest::gtest_main)
gtest_discover_tests(treino_rt_tests DISCOVERY_MODE PRE_TEST)
