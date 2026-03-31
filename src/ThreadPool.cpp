#include "ThreadPool.h"
ThreadPool::ThreadPool(size_t max_tasks) {
    tasks.reserve(max_tasks);
    size_t num_threads = std::max(1U, std::thread::hardware_concurrency());
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this] { return stop || !tasks.empty(); });
                    if (stop && tasks.empty()) return;
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
    pending_cv.wait(lock, [this] { return pending_tasks == 0; });
}