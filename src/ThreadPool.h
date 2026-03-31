#pragma once
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>

using Task = std::function<void()>;

// 线程池
// 并非性能热点
// 用于简化的额外信息
// 1. 所有Task类型一致，为std::function<void()>，且不会抛出异常
// 2. void wait_all_idle()在被调用到结束调用这段时间内不会有新的任务被添加到线程池中
// 3. 每次循环会有height个task添加进线程池，然后调用wait_all_idle()等待所有任务完成，其中height为定值
// 4. Task无先后次序要求，可以使用stack、queue、deque等结构来存储任务
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
    explicit ThreadPool(size_t max_tasks = 2160);
    ~ThreadPool();
    void enqueue(Task task);
    // Assume that no new tasks will be added after this call
    void wait_all_idle();
};