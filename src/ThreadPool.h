#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <functional>

using Task = std::function<void()>;

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<Task> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop;
    std::condition_variable idle_cv;
    std::atomic<std::size_t> active_threads;
public:
    explicit ThreadPool(size_t n = std::thread::hardware_concurrency());
    ~ThreadPool();
    void enqueue(Task task);
    // Assume that no new tasks will be added after this call
    void wait_all_idle();
};