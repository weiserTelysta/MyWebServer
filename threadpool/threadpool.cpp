#include "threadpool.h"

// 创建线程
ThreadPool::ThreadPool(size_t thread_number): m_stop(false){
    for(size_t i=0; i< thread_number; ++i){
        m_workers.emplace_back([this]{
            // 在无限循环中，让工作线程寻找工作
            while(true){
                std::function<void()> task;

                {
                    //RAII原理作用域
                    std::unique_lock<std::mutex> lock(this->m_queue_mutex);

                    // wait自动解锁并让线程睡眠，当唤醒时，自动重新加锁
                    // 唤醒条件：线程池停止了，或者是队列里有任务了
                    this->m_condition.wait(lock,[this]{
                        return this->m_stop || !this->m_tasks.empty();
                    });

                    // 如果线程池停止，且任务做完了的情况。
                    if(this->m_stop && this->m_tasks.empty()){
                        return;
                    }

                    // 拿到任务
                    task = std::move(this->m_tasks.front());
                    this->m_tasks.pop();
                } // RAII作用域结束，lock对象自动销毁，自动释放锁。

                // 开始执行任务
                task();
            }
        });
    }
}

void ThreadPool::enqueue(std::function<void()> task){
    {
        //RAII 作用域
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_tasks.push(std::move(task));
    } // RAII作用域结束。

    //唤醒一个正在睡眠的工作线程机进行工作
    m_condition.notify_one();
}

// 析构函数
ThreadPool::~ThreadPool(){
    {// RAII作用域
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_stop =true;
    } // RAII作用域结束

    // 唤醒所有等待中的线程，告诉他们应该下班了。
    m_condition.notify_all();

    // 等待所有线程把工作处理完进行销毁
    for(std::thread& worker : m_workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}