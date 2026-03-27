#include "broker/thread_pool.h"
#include <spdlog/spdlog.h>

namespace broker {

ThreadPool::ThreadPool(size_t num_threads, size_t max_queue)
    : max_queue_(max_queue) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i)
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    spdlog::info("[threadpool] Started {} worker threads (max_queue={})",
                 num_threads, max_queue);
}

ThreadPool::~ThreadPool() { stop(); }

bool ThreadPool::submit(std::function<void()> task) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (stopping_) return false;
    if (tasks_.size() >= max_queue_) {
        spdlog::warn("[threadpool] Queue full ({}), dropping oldest task", max_queue_);
        tasks_.pop(); // shed oldest — backpressure
    }
    tasks_.push(std::move(task));
    cv_.notify_one();
    return true;
}

void ThreadPool::stop() {
    stopping_ = true;
    cv_.notify_all();
    for (auto& w : workers_)
        if (w.joinable()) w.join();
    workers_.clear();
}

size_t ThreadPool::queue_depth() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return tasks_.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]{ return !tasks_.empty() || stopping_; });
            if (stopping_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try { task(); }
        catch (const std::exception& e) {
            spdlog::error("[threadpool] Task threw: {}", e.what());
        }
    }
}

} // namespace broker
