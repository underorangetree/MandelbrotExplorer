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
    size_t pending_tasks = 0;
    std::condition_variable pending_cv;
public:
    explicit ThreadPool(size_t max_tasks);
    ~ThreadPool();
    void enqueue(Task task);
    void wait_all_idle();
};