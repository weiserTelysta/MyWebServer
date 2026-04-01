#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<vector>
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>

class ThreadPool{
public:
    // 构造函数，指定线程数量
    explicit ThreadPool(size_t thread_number = 8);

    // 析构函数
    ~ThreadPool();

    // 核心接口：添加任务
    void enqueue(std::function<void()> task);
private:
    std::vector<std::thread> m_workers;             // 线程数组
    std::queue<std::function<void()>> m_tasks;      // 任务队列

    std::mutex m_queue_mutex;                       // 互斥锁
    std::condition_variable m_condition;            // 条件变量
    bool m_stop;                                    // 停止标志
};

#endif