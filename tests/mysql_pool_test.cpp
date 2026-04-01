#include "mysql_connection.h"
#include <iostream>
#include <thread>
#include <vector>

void task(int id) {
    MYSQL* conn = nullptr;
    {
        ConnectionGuard guard(&conn, connection_pool::GetInstance());
        if (conn) {
            std::cout << "Thread " << id << " GOT connection! Doing work..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 模拟查询耗时
        } else {
            std::cout << "Thread " << id << " failed to get connection!" << std::endl;
        }
    } // 自动归还
    std::cout << "Thread " << id << " released connection." << std::endl;
}

int main() {
    std::cout << "Initializing Connection Pool..." << std::endl;
    connection_pool* pool = connection_pool::GetInstance();
    
    // 【关键】对应你的 docker-compose.yml 
    // IP: 127.0.0.1 (不要用 localhost)
    // 账号: root, 密码: root, 库名: webdb, 暴露的端口: 3307
    // 最大连接数先设为 4，用来测试排队
    pool->init("127.0.0.1", "root", "root", "webdb", 3307, 4, 0);
    std::cout << "Pool initialized with " << pool->GetFreeConn() << " free connections." << std::endl;

    // 启动 10 个线程去抢 4 个连接
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(task, i);
    }

    for (auto& t : threads) {
        if(t.joinable()) t.join();
    }

    std::cout << "All requests handled. Remaining connections: " << pool->GetFreeConn() << std::endl;
    return 0;
}