#include "thread_pool.hpp"

namespace workbench {

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) num_threads = 1;

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);
                    this->cv_.wait(lock, [this]() {
                        return this->stop_ || !this->tasks_.empty();
                    });

                    if (this->stop_ && this->tasks_.empty()) {
                        return;
                    }

                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }

                try {
                    task();
                } catch (...) {
                    // Suppress uncaught exceptions inside worker thread
                }

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);
                    this->active_tasks_--;
                    if (this->active_tasks_ == 0 && this->tasks_.empty()) {
                        this->cv_all_done_.notify_all();
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    cv_all_done_.wait(lock, [this]() {
        return tasks_.empty() && active_tasks_ == 0;
    });
}

void ThreadPool::cancel() {
    cancel_requested_ = true;
    std::unique_lock<std::mutex> lock(queue_mutex_);
    // Drain pending tasks
    while (!tasks_.empty()) {
        tasks_.pop();
        active_tasks_--;
    }
    if (active_tasks_ == 0) {
        cv_all_done_.notify_all();
    }
}

} // namespace workbench
