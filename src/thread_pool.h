#pragma once
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Submit a task.  Thread-safe.
    void enqueue(std::function<void()> task);

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&)  = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void worker_loop();

    std::vector<std::thread>  workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex   mu_;
    std::condition_variable  cv_;
    std::atomic<bool> stop_{false};
};