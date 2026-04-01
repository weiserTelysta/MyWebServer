#include "log.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace std;

// 模拟工作线程疯狂写日志
void worker_thread_func(int thread_id) {
  for (int i = 0; i < 1000; ++i) {
    // 交替测试不同级别的宏
    if (i % 4 == 0) {
      LOG_DEBUG("来自线程 [%d] 的 Debug 日志: 当前计数 %d", thread_id, i);
    } else if (i % 4 == 1) {
      LOG_INFO("来自线程 [%d] 的 Info 日志: 当前计数 %d", thread_id, i);
    } else if (i % 4 == 2) {
      LOG_WARN("来自线程 [%d] 的 Warn 日志: 当前计数 %d", thread_id, i);
    } else {
      LOG_ERROR("来自线程 [%d] 的 Error 日志: 当前计数 %d", thread_id, i);
    }

    // 稍微睡一小会儿，模拟真实业务耗时
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}

int main() {
  cout << "========== 日志系统测试开始 ==========" << endl;

  // 1. 初始化日志系统
  // 参数: 文件名("./ServerLog"), 关闭日志(0=开启), 缓冲区大小(8192),
  // 单个文件最大行数(设定为2000，方便测试自动分文件),
  // 阻塞队列最大容量(1000，大于0表示开启异步模式！)
  bool ret = Log::get_instance()->init("./ServerLog", 0, 8192, 2000, 1000);
  if (!ret) {
    cerr << "日志系统初始化失败！请检查是否有目录权限。" << endl;
    return -1;
  }

  LOG_INFO("日志系统初始化成功！开启异步多线程轰炸测试...");

  // 2. 创建 5 个线程，每个线程写 1000 条日志，总共 5000 条
  vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back(worker_thread_func, i);
  }

  // 3. 等待所有线程工作结束
  for (auto& t : threads) {
    t.join();
  }

  LOG_INFO("所有线程写入完毕！准备退出...");
  cout << "========== 日志系统测试结束 ==========" << endl;

  // main 函数结束时，单例 Log 对象会自动析构，
  // 析构函数里的 join
  // 会保证后台异步写文件的线程把队列里的日志全部刷入磁盘后再安全退出。
  return 0;
}