#ifndef LST_TIMER_HPP
#define LST_TIMER_HPP

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>  // 引入高精度时钟
#include <functional>
#include <mutex>
#include <set>  // 引入红黑树

#include "log.h"

class util_timer;

struct client_data {
  sockaddr_in address;
  int sockfd;
  util_timer* timer;
};

class util_timer {
 public:
  util_timer() : user_data(nullptr) {}

 public:
  // 单调递增时钟，防止系统时间被修改导致定时器错乱
  std::chrono::steady_clock::time_point expire;

  std::function<void(client_data*)> cb_func;

  client_data* user_data;
};

struct TimerCmp {
  bool operator()(const util_timer* a, const util_timer* b) const {
    // 按过期时间排序
    if (a->expire != b->expire) {
      return a->expire < b->expire;
    }
    // 如果过期时间一模一样（并发极高时可能发生），按内存地址排序，防止被
    // set认为是同一个元素而丢弃
    return a < b;
  }
};

class timer_manager {
 public:
  timer_manager() = default;
  ~timer_manager();  // 析构函数中需要释放红黑树里的定时器内存

  // 防止双重释放
  timer_manager(const timer_manager&) = delete;
  timer_manager& operator=(const timer_manager&) = delete;

  void add_timer(util_timer* timer);

  // 由于不能直接修改 set 中的key，续命操作必须把新的过期时间传进来
  void adjust_timer(util_timer* timer,
                    std::chrono::steady_clock::time_point new_expire);

  void del_timer(util_timer* timer);

  void tick();

 private:
  std::mutex m_mutex;

  // 底层数据结构换成基于红黑树的 set
  std::set<util_timer*, TimerCmp> m_timers;
};

class Utils {
 public:
  Utils() {}
  ~Utils() {}

  void init(int timeslot);

  // 对文件描述符设置非阻塞
  int setnonblocking(int fd);

  // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
  void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

  // 信号处理函数
  static void sig_handler(int sig);

  // 设置信号函数
  void addsig(int sig, void(handler)(int), bool restart = true);

  // 定时处理任务，重新定时以不断触发SIGALRM信号
  void timer_handler();

  void show_error(int connfd, const char* info);

 public:
  static int* u_pipefd;
  timer_manager m_timer_lst;  // 替换为新的定时器管理类
  static int u_epollfd;
  int m_TIMESLOT;
};

void cb_func(client_data* user_data);

#endif