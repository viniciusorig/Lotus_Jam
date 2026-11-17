// Separate executable: replaces global operator new to count allocations, so the main
// treino_tests binary is not affected.
#include <gtest/gtest.h>

#include <cstdlib>
#include <new>
#include <vector>

#include "audio/click_scheduler.h"

namespace {
bool g_armed = false;
long g_allocs = 0;
}  // namespace

void* operator new(std::size_t n) {
    if (g_armed) ++g_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

TEST(ClickSchedulerRT, RenderDoesNotAllocate) {
    ClickScheduler s(48000.0, 120, 4);
    std::vector<float> buf(960 * 2);
    BeatMark m[8];
    g_allocs = 0;
    g_armed = true;
    for (int i = 0; i < 1000; ++i) {
        s.render(buf.data(), 960, static_cast<int64_t>(i) * 960);
        s.beatsIn(static_cast<int64_t>(i) * 960, 960, m, 8);
    }
    g_armed = false;
    EXPECT_EQ(g_allocs, 0);
}
