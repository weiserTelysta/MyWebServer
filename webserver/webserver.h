#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <memory>  // 引入智能指针
#include <string>

#include "http_conn.h"
#include "threadpool.h"
#include "timer.h"

const int MAX_FD = 65536;            // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000;  // 最大事件数
const int TIMESLOT = 5;              // 最小超时单位

class WebServer {
 public:
  WebServer();
  ~WebServer();

  void init(int port, std::string user, std::string passWord,
            std::string databaseName, int log_write, int opt_linger,
            int trigmode, int sql_num, int thread_num, int close_log,
            int actor_model);

  void eventLoop();

 private:
  // 内部初始化辅助函数
  void thread_pool();
  void sql_pool();
  void log_write();
  void trig_mode();
  void eventListen();

  // 内部事件处理辅助函数
  void timer(int connfd, struct sockaddr_in client_address);
  void adjust_timer(util_timer* timer);
  void deal_timer(util_timer* timer, int sockfd);
  bool dealclientdata();
  bool dealwithsignal(bool& timeout, bool& stop_server);
  void dealwithread(int sockfd);
  void dealwithwrite(int sockfd);

 private:
  int m_port;
  std::string m_root;
  int m_log_write;
  int m_close_log;
  int m_actormodel;

  int m_pipefd[2];
  int m_epollfd;
  int m_listenfd;
  int m_OPT_LINGER;
  int m_TRIGMode;
  int m_LISTENTrigmode;
  int m_CONNTrigmode;

  // 数据库相关
  connection_pool* m_connPool;
  std::string m_user;
  std::string m_passWord;
  std::string m_databaseName;
  int m_sql_num;

  epoll_event events[MAX_EVENT_NUMBER];
  Utils utils;

  // 智能指针管理的大型资源
  std::unique_ptr<http_conn[]> users;
  std::unique_ptr<client_data[]> users_timer;
  std::unique_ptr<ThreadPool> m_pool;
  int m_thread_num;
};

#endif