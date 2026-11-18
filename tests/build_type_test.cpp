#include <gtest/gtest.h>

// Guards the default-build-type rule in CMakeLists.txt: an unoptimised (-O0) default makes the DSP
// tests ~6x slower. Debug is the only configuration allowed to be unoptimised.
#ifndef TREINO_DEBUG_BUILD
TEST(BuildType, NonDebugBuildIsOptimised) {
#ifdef __OPTIMIZE__
  SUCCEED();
#else
  FAIL() << "not a Debug build but compiled without optimisation: check CMAKE_BUILD_TYPE";
#endif
}
#endif
