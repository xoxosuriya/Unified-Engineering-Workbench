#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

namespace workbench {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Enqueue task for execution
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("enqueue called on stopped ThreadPool");
            }
            active_tasks_++;
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return res;
    }

    // Wait until all enqueued tasks have completed execution
    void wait_all();

    // Request cooperative cancellation
    void cancel();

    // Check if cancellation was requested
    bool is_cancelled() const { return cancel_requested_.load(); }

    // Reset cancellation flag
    void reset_cancel() { cancel_requested_.store(false); }

    // Query active thread count
    size_t worker_count() const { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::condition_variable cv_all_done_;
    
    std::atomic<bool> stop_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<size_t> active_tasks_{0};
};

} // namespace workbench
