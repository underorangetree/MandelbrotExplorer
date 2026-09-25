#include "ThreadPool.h"
#include <algorithm>
#include <stdexcept>

ThreadPool::ThreadPool(int max_tasks) {
    tasks.reserve(max_tasks);
    const int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    try {
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this] () -> bool { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) {
                            return;
                        }
                        task = std::move(tasks.back());
                        tasks.pop_back();
                    }
                    task();
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        --pending_tasks;
                        if (pending_tasks == 0) {
                            pending_cv.notify_all();
                        }
                    }
                }
            });
        }
    } catch (...) {
        // A worker could not be created. Stop and join the workers that were
        // created so that destroying the joinable std::thread objects during
        // unwinding does not call std::terminate, then report the failure.
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }
        cv.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
        throw;
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mtx);
        stop = true;
    }
    cv.notify_all();
    for (std::thread &worker : workers) {
        worker.join();
    }
}

void ThreadPool::enqueue(Task task) {
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (stop) {
            throw std::runtime_error("Try to add Task on stopped ThreadPool");
        }
        tasks.emplace_back(std::move(task));
        ++pending_tasks;
    }
    cv.notify_one();
}

void ThreadPool::wait_all_idle() {
    std::unique_lock<std::mutex> lock(mtx);
    pending_cv.wait(lock, [this] () -> bool { return pending_tasks == 0; });
}

auto ThreadPool::thread_count() const -> int {
    return static_cast<int>(workers.size());
}
