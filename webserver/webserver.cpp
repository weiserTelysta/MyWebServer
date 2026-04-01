#include "webserver.h"

#include <linux/limits.h>

#include <chrono>

WebServer::WebServer() {
  // 使用 std::make_unique 分配大数组，自动防泄漏
  users = std::make_unique<http_conn[]>(MAX_FD);
  users_timer = std::make_unique<client_data[]>(MAX_FD);

  char server_path[PATH_MAX];
  if (getcwd(server_path, sizeof(server_path)) != nullptr) {
    m_root = std::string(server_path) + "/root";
  } else {
    m_root = "./root";  // 万一获取失败的保底方案
  }
}

WebServer::~WebServer() {
  close(m_epollfd);
  close(m_listenfd);
  close(m_pipefd[1]);
  close(m_pipefd[0]);

  // std::string 自己会释放内存
}

void WebServer::init(int port, string user, string passWord,
                     string databaseName, int log_write, int opt_linger,
                     int trigmode, int sql_num, int thread_num, int close_log,
                     int actor_model) {
  // 1. 赋值基础配置参数
  m_port = port;
  m_user = user;
  m_passWord = passWord;
  m_databaseName = databaseName;
  m_sql_num = sql_num;
  m_thread_num = thread_num;
  m_log_write = log_write;
  m_OPT_LINGER = opt_linger;
  m_TRIGMode = trigmode;
  m_close_log = close_log;
  m_actormodel = actor_model;

  // 内部自动装配所有模块
  this->log_write();    // 第一步：先起日志系统（这样后面的报错才能记录）
  this->sql_pool();     // 第二步：连上数据库
  this->thread_pool();  // 第三步：启动多线程池
  this->trig_mode();    // 第四步：设置 Epoll 的触发模式
  this->eventListen();  // 第五步：开启 Socket 监听，准备接客
}

void WebServer::trig_mode() {
  // LT + LT
  if (0 == m_TRIGMode) {
    m_LISTENTrigmode = 0;
    m_CONNTrigmode = 0;
  }
  // LT + ET
  else if (1 == m_TRIGMode) {
    m_LISTENTrigmode = 0;
    m_CONNTrigmode = 1;
  }
  // ET + LT
  else if (2 == m_TRIGMode) {
    m_LISTENTrigmode = 1;
    m_CONNTrigmode = 0;
  }
  // ET + ET
  else if (3 == m_TRIGMode) {
    m_LISTENTrigmode = 1;
    m_CONNTrigmode = 1;
  }
}

void WebServer::log_write() {
  if (0 == m_close_log) {
    // 初始化日志
    if (1 == m_log_write)
      Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
    else
      Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
  }
}

void WebServer::sql_pool() {
  // 初始化数据库连接池
  m_connPool = connection_pool::GetInstance();
  m_connPool->init("127.0.0.1", m_user, m_passWord, m_databaseName, 3307,
                   m_sql_num, m_close_log);

  // 初始化数据库读取表
  users[0].initmysql_result(m_connPool);  // 注意智能指针直接使用 [0]
}

void WebServer::thread_pool() {
  // 🚀 2. 线程池初始化升级：适配你手写的现代 ThreadPool
  m_pool = std::make_unique<ThreadPool>(m_thread_num);
}

void WebServer::eventListen() {
  // 网络编程基础步骤
  m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(m_listenfd >= 0);

  // 优雅关闭连接
  if (0 == m_OPT_LINGER) {
    struct linger tmp = {0, 1};
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
  } else if (1 == m_OPT_LINGER) {
    struct linger tmp = {1, 1};
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
  }

  int ret = 0;
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(m_port);

  int flag = 1;
  setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
  ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
  assert(ret >= 0);
  ret = listen(m_listenfd, 5);
  assert(ret >= 0);

  utils.init(TIMESLOT);

  // epoll创建内核事件表
  epoll_event events[MAX_EVENT_NUMBER];
  m_epollfd = epoll_create(5);
  assert(m_epollfd != -1);

  utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
  http_conn::m_epollfd = m_epollfd;

  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
  assert(ret != -1);
  utils.setnonblocking(m_pipefd[1]);
  utils.addfd(m_epollfd, m_pipefd[0], false, 0);

  utils.addsig(SIGPIPE, SIG_IGN);
  utils.addsig(SIGALRM, utils.sig_handler, false);
  utils.addsig(SIGTERM, utils.sig_handler, false);

  alarm(TIMESLOT);

  // 工具类,信号和描述符基础操作
  Utils::u_pipefd = m_pipefd;
  Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
  users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode,
                     m_close_log, m_user, m_passWord, m_databaseName);

  // 初始化client_data数据
  users_timer[connfd].address = client_address;
  users_timer[connfd].sockfd = connfd;
  util_timer* timer = new util_timer;
  timer->user_data = &users_timer[connfd];
  timer->cb_func = cb_func;

  // 🚀 3. 定时器改造：使用 chrono 获取高精度时间
  auto cur = std::chrono::steady_clock::now();
  timer->expire = cur + std::chrono::seconds(3 * TIMESLOT);

  users_timer[connfd].timer = timer;
  utils.m_timer_lst.add_timer(timer);
}

void WebServer::adjust_timer(util_timer* timer) {
  // 🚀 4. 续命逻辑改造：将新时间传给基于红黑树的 adjust_timer
  auto cur = std::chrono::steady_clock::now();
  auto new_expire = cur + std::chrono::seconds(3 * TIMESLOT);
  utils.m_timer_lst.adjust_timer(timer, new_expire);

  LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer* timer, int sockfd) {
  if (!timer) return;
  timer->cb_func(&users_timer[sockfd]);
  if (timer) {
    utils.m_timer_lst.del_timer(timer);
  }

  LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata() {
  struct sockaddr_in client_address;
  socklen_t client_addrlength = sizeof(client_address);
  if (0 == m_LISTENTrigmode) {
    int connfd = accept(m_listenfd, (struct sockaddr*)&client_address,
                        &client_addrlength);
    if (connfd < 0) {
      LOG_ERROR("%s:errno is:%d", "accept error", errno);
      return false;
    }
    if (http_conn::m_user_count >= MAX_FD) {
      utils.show_error(connfd, "Internal server busy");
      LOG_ERROR("%s", "Internal server busy");
      return false;
    }
    timer(connfd, client_address);
  } else {
    while (1) {
      int connfd = accept(m_listenfd, (struct sockaddr*)&client_address,
                          &client_addrlength);
      if (connfd < 0) {
        LOG_ERROR("%s:errno is:%d", "accept error", errno);
        break;
      }
      if (http_conn::m_user_count >= MAX_FD) {
        utils.show_error(connfd, "Internal server busy");
        LOG_ERROR("%s", "Internal server busy");
        break;
      }
      timer(connfd, client_address);
    }
    return false;
  }
  return true;
}

bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
  int ret = 0;
  int sig;
  char signals[1024];
  ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
  if (ret == -1) {
    return false;
  } else if (ret == 0) {
    return false;
  } else {
    for (int i = 0; i < ret; ++i) {
      switch (signals[i]) {
        case SIGALRM: {
          timeout = true;
          break;
        }
        case SIGTERM: {
          stop_server = true;
          break;
        }
      }
    }
  }
  return true;
}

void WebServer::dealwithread(int sockfd) {
  util_timer* timer = users_timer[sockfd].timer;

  // reactor 模式
  if (1 == m_actormodel) {
    if (timer) {
      adjust_timer(timer);
    }

    // 非阻塞的 Reactor：读操作和业务处理全在工作线程完成
    m_pool->enqueue([this, sockfd, timer]() {
      if (users[sockfd].read_once()) {
        users[sockfd].process();  // 读成功，处理业务
      } else {
        // 读失败（如客户端断开），直接在子线程安全地清理连接和定时器！
        deal_timer(timer, sockfd);
      }
    });
  } else {
    // proactor 模式
    if (users[sockfd].read_once()) {
      LOG_INFO("deal with the client(%s)",
               inet_ntoa(users[sockfd].get_address()->sin_addr));
      m_pool->enqueue([this, sockfd]() { users[sockfd].process(); });
      if (timer) {
        adjust_timer(timer);
      }
    } else {
      deal_timer(timer, sockfd);
    }
  }
}

void WebServer::dealwithwrite(int sockfd) {
  util_timer* timer = users_timer[sockfd].timer;

  // reactor 模式
  if (1 == m_actormodel) {
    if (timer) {
      adjust_timer(timer);
    }

    // 真正非阻塞的 Reactor：写操作全在工作线程完成
    m_pool->enqueue([this, sockfd, timer]() {
      // users[sockfd].write() 如果返回 false，说明写失败或对端断开
      if (!users[sockfd].write()) {
        deal_timer(timer, sockfd);
      }
    });
  } else {
    // proactor 模式（保持不变）
    if (users[sockfd].write()) {
      LOG_INFO("send data to the client(%s)",
               inet_ntoa(users[sockfd].get_address()->sin_addr));
      if (timer) {
        adjust_timer(timer);
      }
    } else {
      deal_timer(timer, sockfd);
    }
  }
}

void WebServer::eventLoop() {
  bool timeout = false;
  bool stop_server = false;

  while (!stop_server) {
    int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR) {
      LOG_ERROR("%s", "epoll failure");
      break;
    }

    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;

      // 处理新到的客户连接
      if (sockfd == m_listenfd) {
        bool flag = dealclientdata();
        if (false == flag) continue;
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // 服务器端关闭连接，移除对应的定时器
        util_timer* timer = users_timer[sockfd].timer;
        deal_timer(timer, sockfd);
      }
      // 处理信号
      else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
        bool flag = dealwithsignal(timeout, stop_server);
        if (false == flag) LOG_ERROR("%s", "dealclientdata failure");
      }
      // 处理客户连接上接收到的数据
      else if (events[i].events & EPOLLIN) {
        dealwithread(sockfd);
      } else if (events[i].events & EPOLLOUT) {
        dealwithwrite(sockfd);
      }
    }
    if (timeout) {
      utils.timer_handler();
      LOG_INFO("%s", "timer tick");
      timeout = false;
    }
  }
}