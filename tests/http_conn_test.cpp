#include "http_conn.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

#include "mysql_connection.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

int main() {
  std::cout << "========== HTTP Connection Test Start ==========" << std::endl;

  // 1. 初始化数据库连接池 (请修改为你真实的数据库密码和库名)
  connection_pool* connPool = connection_pool::GetInstance();
  connPool->init("127.0.0.1", "root", "root", "webdb", 3307, 8, 0);

  // 2. 预先分配 HTTP 连接对象池，并加载数据库的账号密码到静态 map 中
  http_conn* users = new http_conn[MAX_FD];
  users->initmysql_result(connPool);

  // 3. 创建测试用的监听大门 (Socket)
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  int flag = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag,
             sizeof(flag));  // 端口复用

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(9999);  // 专门开个 9999 端口用于测试
  bind(listenfd, (struct sockaddr*)&address, sizeof(address));
  listen(listenfd, 5);

  // 4. 创建安保监控 epoll
  epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);
  http_conn::m_epollfd = epollfd;  // 赋值给静态变量

  // 把监听大门挂到 epoll 上
  epoll_event event;
  event.data.fd = listenfd;
  event.events = EPOLLIN | EPOLLRDHUP;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);

  std::cout << "[SUCCESS] Server is listening on port 9999." << std::endl;
  std::cout << "[ACTION] Open your browser and visit: http://127.0.0.1:9999/"
            << std::endl;

  // 5. 单线程事件循环 (剥离了线程池，直接在主线程处理业务，极其适合断点调试！)
  while (true) {
    int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;

      // 动静 A：有新浏览器连进来了
      if (sockfd == listenfd) {
        struct sockaddr_in client_address;
        socklen_t client_addrlength = sizeof(client_address);
        int connfd = accept(listenfd, (struct sockaddr*)&client_address,
                            &client_addrlength);

        // 获取当前工作目录，并拼接出 root 网页根目录
        char server_path[200];
        getcwd(server_path, 200);
        char root_path[256];
        snprintf(root_path, sizeof(root_path), "%s/root", server_path);

        // 初始化这个客人的专属 http_conn
        users[connfd].init(connfd, client_address, root_path, 0, 0, "root",
                           "你的密码", "你的库名");
        std::cout << "\n[NEW] Client connected! fd: " << connfd << std::endl;
      }
      // 动静 B：客户端异常断开
      else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        std::cout << "[DISCONNECT] Client left. fd: " << sockfd << std::endl;
        users[sockfd].close_conn();
      }
      // 动静 C：浏览器发来 HTTP 请求了！
      else if (events[i].events & EPOLLIN) {
        std::cout << "[READ] Receiving data from fd: " << sockfd << std::endl;
        if (users[sockfd].read_once()) {
          // 💥 核心测试点：单步调试跟踪 process 函数！
          users[sockfd].process();
        } else {
          users[sockfd].close_conn();
        }
      }
      // 动静 D：网卡空闲，把网页文件写回给浏览器！
      else if (events[i].events & EPOLLOUT) {
        std::cout << "[WRITE] Sending response to fd: " << sockfd << std::endl;
        if (!users[sockfd].write()) {
          users[sockfd].close_conn();
        }
      }
    }
  }

  delete[] users;
  return 0;
}