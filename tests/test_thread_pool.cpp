#include <gtest/gtest.h>
#include "thread_pool.h"
#include <atomic>
#include <chrono>
#include <thread>

TEST(ThreadPool, ExecutesAllTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    const int N = 100;

    for (int i = 0; i < N; ++i) {
        pool.enqueue([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    // Give threads time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(counter.load(), N);
}

TEST(ThreadPool, GracefulShutdown) {
    // Destructor should join all threads without deadlock.
    {
        ThreadPool pool(2);
        for (int i = 0; i < 10; ++i)
            pool.enqueue([] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
    }   // destructor called here
    SUCCEED();
}

TEST(ThreadPool, SingleThread) {
    ThreadPool pool(1);
    std::vector<int> results;
    std::mutex mu;

    for (int i = 0; i < 5; ++i) {
        pool.enqueue([i, &results, &mu] {
            std::lock_guard<std::mutex> lock(mu);
            results.push_back(i);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(results.size(), 5u);
}