#include <iostream>
#include <string>
#include <chrono>
#include "threadpool.h"

std::mutex cout_mutex;

void simulate_hard_work(int task_id){
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "我是打工人 [线程ID："<<std::this_thread::get_id() <<"] 开始工作！\n";
    }

    //模拟耗时操作
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout<<"我是打工人 [线程ID："<<std::this_thread::get_id() <<"] 完成工作！\n";
    }
}

int main(){
    std::cout << "--- 线程池测试开始 ---" << std::endl;

    {
        // setup threadpool
        ThreadPool pool(3);

        for (int i = 1; i <= 8; ++i) {
            pool.enqueue([i] {
                simulate_hard_work(i);
            });
        }

        std::this_thread::sleep_for(std::chrono::seconds(4));
    }

    std::cout << "--- 线程池测试结束，所有线程安全下班 ---" << std::endl;
    return 0;
}