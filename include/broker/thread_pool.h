#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>

namespace broker {

// Fixed-size thread pool.
// submit() enqueues a task; if the queue is at capacity the oldest pending
// task is dropped and a warning is logged (backpressure strategy: shed load
// rather than block the accept loop).
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads, size_t max_queue = 1024);
    ~ThreadPool();

    // Enqueue a task. Returns false if the pool is stopping or queue is full.
    bool submit(std::function<void()> task);

    void stop();

    size_t queue_depth() const;
    size_t thread_count() const { return workers_.size(); }

private:
    void worker_loop();

    std::vector<std::thread>            workers_;
    std::queue<std::function<void()>>   tasks_;
    mutable std::mutex                  mtx_;
    std::condition_variable             cv_;
    std::atomic<bool>                   stopping_{false};
    size_t                              max_queue_;
};

} // namespace broker
