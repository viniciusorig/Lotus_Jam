#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>

// Single-producer / single-consumer lock-free ring buffer.
// Capacity must be a power of two. Exactly one thread may call write()
// and exactly one (other) thread may call read().
template <class T>
class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2)
        : capacity_(capacityPow2),
          mask_(capacityPow2 - 1),
          buf_(std::make_unique<T[]>(capacityPow2)) {
        // Callers must pass a non-zero power of two.
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer side. Returns how many items were actually written.
    size_t write(const T* data, size_t n) noexcept {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        const size_t count = std::min(n, capacity_ - (w - r));
        for (size_t i = 0; i < count; ++i) buf_[(w + i) & mask_] = data[i];
        write_.store(w + count, std::memory_order_release);
        return count;
    }

    // Consumer side. Returns how many items were actually read.
    size_t read(T* out, size_t n) noexcept {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t count = std::min(n, w - r);
        for (size_t i = 0; i < count; ++i) out[i] = buf_[(r + i) & mask_];
        read_.store(r + count, std::memory_order_release);
        return count;
    }

    size_t readable() const noexcept {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }

    size_t writable() const noexcept { return capacity_ - readable(); }

private:
    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<T[]> buf_;
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
};
