#include "ThreadPool.h"
ThreadPool::ThreadPool(size_t n) : stop(false), active_threads(0) {
    for (size_t i = 0; i < n; ++i) {
        workers.emplace_back([this] {
            while (true) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this] { return stop || !tasks.empty(); });
                    if (stop && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                    active_threads.fetch_add(1, std::memory_order_relaxed);
                }
                task();
                if (active_threads.fetch_sub(1, std::memory_order_relaxed) == 1) {
                    idle_cv.notify_all();
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
        tasks.emplace(std::move(task));
    }
    cv.notify_one();
}
void ThreadPool::wait_all_idle() {
    std::unique_lock<std::mutex> lock(mtx);
    idle_cv.wait(lock, [this] { return tasks.empty() && active_threads.load(std::memory_order_relaxed) == 0; });
}