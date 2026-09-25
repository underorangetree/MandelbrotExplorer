#pragma once
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>

using Task = std::function<void()>;

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::vector<Task> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;
    int pending_tasks = 0;
    std::condition_variable pending_cv;
public:
    explicit ThreadPool(int max_tasks);
    ThreadPool(const ThreadPool&) = delete;
    auto operator=(const ThreadPool&) -> ThreadPool& = delete;
    ThreadPool(ThreadPool&&) = delete;
    auto operator=(ThreadPool&&) -> ThreadPool& = delete;
    ~ThreadPool();
    void enqueue(Task task);
    void wait_all_idle();
    [[nodiscard]] auto thread_count() const -> int;
};