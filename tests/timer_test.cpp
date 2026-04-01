#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// 假设你的 timer.h 放在 timer 目录下，请根据实际情况调整头文件路径
#include "timer.h"

using namespace std;

// 模拟的 HTTP 连接数量
const int CONN_COUNT = 1000;

// 模拟连接断开时的回调逻辑
void mock_cb_func(client_data* user_data) {
  if (!user_data) return;
  cout << "  [超时断开] 连接 sockfd: " << user_data->sockfd << " 已清理."
       << endl;
}

// 模拟工作线程：不断有新连接进来，并添加定时器
void worker_add_timer(timer_manager& manager, client_data* clients,
                      int start_idx, int count) {
  for (int i = 0; i < count; ++i) {
    int idx = start_idx + i;
    util_timer* t = new util_timer();

    // 设置过期时间：随机 1~5 秒后过期
    int timeout_ms = 1000 + (rand() % 4000);
    t->expire = chrono::steady_clock::now() + chrono::milliseconds(timeout_ms);

    t->user_data = &clients[idx];
    t->cb_func = mock_cb_func;

    clients[idx].sockfd = idx;
    clients[idx].timer = t;

    manager.add_timer(t);
  }
}

// 模拟工作线程：连接发送了新数据，给定时器续命
void worker_adjust_timer(timer_manager& manager, client_data* clients,
                         int count) {
  for (int i = 0; i < 50; ++i) {  // 随机选50个连接续命
    int idx = rand() % count;
    util_timer* t = clients[idx].timer;
    if (t) {
      // 续命：额外延长 3 秒
      auto new_expire =
          chrono::steady_clock::now() + chrono::milliseconds(3000);
      manager.adjust_timer(t, new_expire);
    }
    this_thread::sleep_for(chrono::milliseconds(10));
  }
}

int main() {
  srand(time(NULL));
  cout << "========== 定时器系统(红黑树)测试开始 ==========" << endl;

  timer_manager manager;
  client_data clients[CONN_COUNT];  // 模拟 1000 个客户端连接

  // 1. 启动多个线程，并发添加定时器
  vector<std::thread> add_threads;
  for (int i = 0; i < 4; ++i) {
    add_threads.emplace_back(worker_add_timer, ref(manager), clients, i * 250,
                             250);
  }

  // 2. 启动一个线程，并发给某些定时器续命
  std::thread adjust_thread(worker_adjust_timer, ref(manager), clients,
                            CONN_COUNT);

  // 等待添加和续命操作基本完成（实际 WebServer 中是持续发生的）
  for (auto& t : add_threads) t.join();
  adjust_thread.join();

  cout << "所有 " << CONN_COUNT
       << " 个定时器已加入树中。开始主循环检测 (tick)..." << endl;

  // 3. 模拟 Event Loop，不断调用 tick() 检查过期定时器
  // 我们跑 8 秒，因为最长超时时间差不多是 5秒(初始) + 3秒(续命)
  auto start_time = chrono::steady_clock::now();
  while (true) {
    manager.tick();

    auto now = chrono::steady_clock::now();
    if (chrono::duration_cast<chrono::seconds>(now - start_time).count() >= 8) {
      break;  // 跑 8 秒后停止测试
    }

    // 每次 tick 后稍微睡一下，防止 CPU 空转 (类似 epoll_wait 的 timeout)
    this_thread::sleep_for(chrono::milliseconds(100));
  }

  cout << "========== 定时器测试结束 ==========" << endl;
  return 0;
}