#include "timer.h"

#include "http_conn.h"

// 基于红黑树实现
timer_manager::~timer_manager() {
  // 释放红黑树中所有定时器的内存
  for (auto timer : m_timers) {
    delete timer;
  }
  m_timers.clear();
}

void timer_manager::add_timer(util_timer* timer) {
  if (!timer) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_mutex);
  m_timers.insert(timer);
}

void timer_manager::adjust_timer(
    util_timer* timer, std::chrono::steady_clock::time_point new_expire) {
  if (!timer) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_timers.find(timer);
  if (it != m_timers.end()) {
    m_timers.erase(it);
    timer->expire = new_expire;
    m_timers.insert(timer);
  }
}

void timer_manager::del_timer(util_timer* timer) {
  if (!timer) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_timers.find(timer);
  if (it != m_timers.end()) {
    m_timers.erase(it);
    delete timer;
  }
}

void timer_manager::tick() {
  if (m_timers.empty()) {
    return;
  }

  auto cur = std::chrono::steady_clock::now();

  for (auto it = m_timers.begin(); it != m_timers.end();) {
    util_timer* timer = *it;

    // set 是按过期时间从小到大排好序的
    // 如果遇到第一个还没过期的，说明后面的也肯定没过期，直接 break 退出
    if (cur < timer->expire) {
      break;
    }

    // 执行回调函数，清理非活动连接
    if (timer->cb_func) {
      timer->cb_func(timer->user_data);
    }

    // C++11 标准中，erase 会返回指向下一个元素的迭代器
    // 这样可以防止当前迭代器失效导致的段错误
    it = m_timers.erase(it);

    delete timer;  // 清理定时器对象
  }
}

// // ai 给的一个改进方案，有时间回来研究一下。
// void timer_manager::tick() {
//     std::unique_lock<std::mutex> lock(m_mutex);
//     if (m_timers.empty()) return;

//     auto cur = std::chrono::steady_clock::now();

//     while (!m_timers.empty()) {
//         auto it = m_timers.begin();
//         util_timer* timer = *it;

//         if (cur < timer->expire) break;

//         // 1. 立即从容器移除，断绝其他线程通过 find 找到它的可能
//         m_timers.erase(it);

//         // 2. 解锁！让回调函数自由飞翔，不影响其他线程 add/adjust
//         lock.unlock();

//         if (timer->cb_func) {
//             timer->cb_func(timer->user_data);
//         }
//         delete timer;

//         // 3. 处理完一个，重新加锁拿回容器控制权，检查下一个
//         lock.lock();
//     }
// }

void Utils::init(int timeslot) { m_TIMESLOT = timeslot; }

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;

  if (1 == TRIGMode)
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  else
    event.events = EPOLLIN | EPOLLRDHUP;

  if (one_shot) event.events |= EPOLLONESHOT;

  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig) {
  int save_errno = errno;
  int msg = sig;
  send(u_pipefd[1], (char*)&msg, 1, 0);
  errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = handler;
  if (restart) sa.sa_flags |= SA_RESTART;
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
  m_timer_lst.tick();
  alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char* info) {
  send(connfd, info, strlen(info), 0);
  close(connfd);
}

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

void cb_func(client_data* user_data) {
  if (!user_data) return;
  epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
  // assert(user_data);
  close(user_data->sockfd);
  http_conn::m_user_count--;
}