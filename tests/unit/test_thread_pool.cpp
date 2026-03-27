#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "broker/thread_pool.h"

using namespace broker;

TEST(ThreadPool, ExecutesTasks) {
    ThreadPool pool(4, 100);
    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i)
        pool.submit([&counter]{ counter++; });
    // Give workers time to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pool.stop();
    EXPECT_EQ(counter.load(), 20);
}

TEST(ThreadPool, ReturnsfalseAfterStop) {
    ThreadPool pool(2, 10);
    pool.stop();
    bool accepted = pool.submit([]{ });
    EXPECT_FALSE(accepted);
}

TEST(ThreadPool, QueueDepthReflectsBacklog) {
    // Single slow worker — depth should grow
    ThreadPool pool(1, 1000);
    std::atomic<bool> gate{false};
    // Block the single worker
    pool.submit([&gate]{ while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // Enqueue more tasks
    for (int i = 0; i < 10; ++i) pool.submit([]{});
    EXPECT_GE(pool.queue_depth(), 5u);
    gate = true;
    pool.stop();
}

TEST(ThreadPool, SheddingDoesNotCrash) {
    // max_queue = 5 — overflow should shed, not crash
    ThreadPool pool(1, 5);
    std::atomic<bool> gate{false};
    pool.submit([&gate]{ while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    for (int i = 0; i < 20; ++i)
        pool.submit([]{});
    gate = true;
    pool.stop();
    SUCCEED(); // no crash / deadlock
}
