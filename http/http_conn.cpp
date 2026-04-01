#include "http_conn.h"

#include <mysql/mysql.h>

#include <fstream>
#include <iostream>

// 静态成员的初始化
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
std::map<std::string, std::string> http_conn::m_users;  // 全局唯一变量缓存表
std::mutex http_conn::m_user_mutex;                     // 保护m_users的互斥锁

// 定义http想要的状态信息
namespace {
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form =
    "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form =
    "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form =
    "There was an unusual problem serving the request file.\n";
}  // namespace

// 初始化数据库查询
void http_conn::initmysql_result(connection_pool* connPool) {
  MYSQL* mysql = nullptr;
  // 使用RAII守卫，自动向连接池借一个连接，离开函数自动还。
  ConnectionGuard mysqlcon(&mysql, connPool);

  // 在 user 表中检索 username,passwd 数据
  if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
    // 暂时不用Log系统
    std::cerr << "SELECT error:" << mysql_error(mysql) << std::endl;
    return;
  }

  // 从表中检索完整的结果集
  MYSQL_RES* result = mysql_store_result(mysql);
  if (!result) return;

  // 从结果集中获取下一行，将对应的用户名和密码，存入静态共享的 map 中
  while (MYSQL_ROW row = mysql_fetch_row(result)) {
    std::string temp1(row[0]);
    std::string temp2(row[1]);
    m_users[temp1] = temp2;  // 存入共享内存
  }

  // 释放结果集内存
  mysql_free_result(result);
}

// 对文件描述符设置为非阻塞
int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

// 将内核时间表注册读事件、ET模式，选择开启EOPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;

  if (1 == TRIGMode) {
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  } else {
    event.events = EPOLLIN | EPOLLRDHUP;
  }

  if (one_shot) {
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
  close(fd);
}

// 将事件重置为 EPOLLONSHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;

  if (1 == TRIGMode) {
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
  } else {
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
  }

  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 关闭连接，客户总量减一
void http_conn::close_conn(bool real_close) {
  if (real_close && (m_sockfd != -1)) {
    // 等会还要进行log改造。
    std::cout << "close " << m_sockfd << std::endl;
    removefd(m_epollfd, m_sockfd);
    m_sockfd = -1;
    m_user_count--;
  }
}

// 初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr, std::string root,
                     int TRIGMode, int close_log, std::string user,
                     std::string passwd, std::string sqlname) {
  m_sockfd = sockfd;
  m_address = addr;

  addfd(m_epollfd, sockfd, true, m_TRIGMode);
  m_user_count++;

  strncpy(doc_root, root.c_str(), FILENAME_LEN - 1);
  doc_root[FILENAME_LEN - 1] = '\0';

  m_TRIGMode = TRIGMode;
  m_close_log = close_log;

  strncpy(sql_user, user.c_str(), 99);
  sql_user[99] = '\0';
  strncpy(sql_passwd, passwd.c_str(), 99);
  sql_passwd[99] = '\0';
  strncpy(sql_name, sqlname.c_str(), 99);
  sql_name[99] = '\0';

  init();
}

// 初始化新接受的连接
void http_conn::init() {
  bytes_to_send = 0;
  bytes_have_send = 0;
  m_check_state = CHECK_STATE_REQUESTLINE;
  m_linger = false;
  m_method = GET;
  m_url = 0;
  m_version = 0;
  m_content_length = 0;
  m_host = 0;
  m_start_line = 0;
  m_checked_idx = 0;
  m_read_idx = 0;
  m_write_idx = 0;
  cgi = 0;
  m_state = 0;
  timer_flag = 0;
  improv = 0;

  memset(m_read_buf, '\0', READ_BUFFER_SIZE);
  memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
  memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机
// 寻找\r\n，强行截断
http_conn::LINE_STATUS http_conn::parse_line() {
  char temp;
  for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
    temp = m_read_buf[m_checked_idx];
    if (temp == '\r') {
      if ((m_checked_idx + 1) == m_read_idx) {
        return LINE_OPEN;
      } else if (m_read_buf[m_checked_idx + 1] == '\n') {
        m_read_buf[m_checked_idx++] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    } else if (temp == '\n') {
      if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
        m_read_buf[m_checked_idx - 1] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read_once() {
  if (m_read_idx >= READ_BUFFER_SIZE) {
    return false;
  }
  int bytes_read = 0;

  // LT读取数据
  if (0 == m_TRIGMode) {  // LT读取数据
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                      READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;
    if (bytes_read <= 0) {
      return false;
    }
    return true;
  } else {  // ET读取数据
    while (true) {
      bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                        READ_BUFFER_SIZE - m_read_idx, 0);
      if (-1 == bytes_read) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        return false;
      } else if (bytes_read == 0) {
        return false;
      }
      m_read_idx += bytes_read;
    }
    return true;
  }
}

// 解析 http 请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
  // 1. 提取 METHOD
  m_url = strpbrk(text, " \t");  // 找到第一个空格或制表符
  if (!m_url) return BAD_REQUEST;

  *m_url++ = '\0';  // 将空格置为 \0，此时 text 就是 "GET" 或 "POST"
  char* method = text;
  if (strcasecmp(method, "GET") == 0) {
    m_method = GET;
  } else if (strcasecmp(method, "POST") == 0) {
    m_method = POST;
    cgi = 1;
  } else {
    return BAD_REQUEST;
  }

  // 2. 提取 VERSION (注意：必须先找版本，再处理 URL)
  m_url += strspn(m_url, " \t");      // 跳过额外的空格
  m_version = strpbrk(m_url, " \t");  // 寻找 URL 后的空格
  if (!m_version) return BAD_REQUEST;

  *m_version++ = '\0';  // 💥 关键点：截断 URL，让 m_url 变成纯净的路径
  m_version += strspn(m_version, " \t");  // 跳过版本号前的空格

  // 💥 修复点：使用 strncasecmp 仅比较前 8 位，防止因末尾有 \r\n 导致匹配失败
  if (strncasecmp(m_version, "HTTP/1.1", 8) != 0) {
    return BAD_REQUEST;
  }

  // 3. 处理 URL (去除前缀)
  if (strncasecmp(m_url, "http://", 7) == 0) {
    m_url += 7;
    m_url = strchr(m_url, '/');
  } else if (strncasecmp(m_url, "https://", 8) == 0) {
    m_url += 8;
    m_url = strchr(m_url, '/');
  }

  if (!m_url || m_url[0] != '/') {
    return BAD_REQUEST;
  }

  // 4. 默认首页补全
  if (strlen(m_url) == 1) {
    strcat(m_url, "judge.html");
  }

  // 5. 状态转移
  m_check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

// 解析 http 请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
  if (text[0] == '\0') {
    if (m_content_length != 0) {
      m_check_state = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    return GET_REQUEST;
  } else if (strncasecmp(text, "Connection:", 11) == 0) {
    text += 11;
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0) m_linger = true;
  } else if (strncasecmp(text, "Content-length:", 15) == 0) {
    text += 15;
    text += strspn(text, " \t");
    m_content_length = atol(text);
  } else if (strncasecmp(text, "Host:", 5) == 0) {
    text += 5;
    text += strspn(text, " \t");
    m_host = text;
  }
  return NO_REQUEST;
}

// 判断 http 请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
  if (m_read_idx >= (m_content_length + m_checked_idx)) {
    text[m_content_length] = '\0';
    m_string = text;  // POST请求中最后为输入的用户名和密码
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

// 主状态机核心
http_conn::HTTP_CODE http_conn::process_read() {
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char* text = 0;

  while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
         ((line_status = parse_line()) == LINE_OK)) {
    text = get_line();
    m_start_line = m_checked_idx;
    // LOG_INFO("%s",text);

    switch (m_check_state) {
      case CHECK_STATE_REQUESTLINE:
        ret = parse_request_line(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        break;
      case CHECK_STATE_HEADER:
        ret = parse_headers(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        } else if (ret == GET_REQUEST) {
          return do_request();
        }
        break;
      case CHECK_STATE_CONTENT:
        ret = parse_content(text);
        if (ret == GET_REQUEST) {
          return do_request();
        }
        line_status = LINE_OPEN;
        break;
      default:
        return INTERNAL_ERROR;
    }
  }
  return NO_REQUEST;
}

// 业务核心逻辑
http_conn::HTTP_CODE http_conn::do_request() {
  // 1. 拼接根目录
  strcpy(m_real_file, doc_root);
  int len = strlen(doc_root);
  const char* p = strrchr(m_url, '/');

  // 2. 处理 CGI 登录注册逻辑
  if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
    // 提取用户名和密码 (安全版：利用 sscanf 防止溢出)
    char name[100] = {0};
    char password[100] = {0};
    // 假设报文格式为：user=XXX&password=YYY (注意原版可能使用的是
    // passwd，请根据前端一致性调整) %99[^&] 表示最多读取 99 个非 '&'
    // 的字符，防止超长字符串撑爆数组
    sscanf(m_string, "user=%99[^&]&password=%99s", name, password);

    if (*(p + 1) == '3')  // 注册请求
    {
      // 💥 优化 1：使用栈上数组配合 snprintf，彻底消灭 malloc/free 和 strcat
      char sql_insert[256];
      snprintf(sql_insert, sizeof(sql_insert),
               "INSERT INTO user(username, passwd) VALUES('%s', '%s')", name,
               password);

      if (m_users.find(name) == m_users.end()) {
        std::lock_guard<std::mutex> lock(m_user_mutex);
        MYSQL* temp_conn = nullptr;
        ConnectionGuard db_guard(&temp_conn, connection_pool::GetInstance());

        int res = mysql_query(temp_conn, sql_insert);
        if (!res) {
          m_users.insert(std::pair<std::string, std::string>(name, password));
          strcpy(m_url, "/log.html");
        } else {
          strcpy(m_url, "/registerError.html");
        }
      } else {
        strcpy(m_url, "/registerError.html");
      }
    } else if (*(p + 1) == '2')  // 登录请求
    {
      if (m_users.find(name) != m_users.end() && m_users[name] == password)
        strcpy(m_url, "/welcome.html");
      else
        strcpy(m_url, "/logError.html");
    }
  }

  // 3. 💥 优化 2：页面跳转路由配置极简重构
  // 原来那种 malloc 200 个字节再去拼装的做法极度浪费且危险。
  // 直接使用指针指向常量字符串即可。
  const char* target_file = m_url;  // 默认使用当前 URL

  if (*(p + 1) == '0')
    target_file = "/register.html";
  else if (*(p + 1) == '1')
    target_file = "/log.html";
  else if (*(p + 1) == '5')
    target_file = "/picture.html";
  else if (*(p + 1) == '6')
    target_file = "/video.html";
  else if (*(p + 1) == '7')
    target_file = "/fans.html";

  // 统一在这里进行一次字符串拼接
  strncpy(m_real_file + len, target_file, FILENAME_LEN - len - 1);
  m_real_file[FILENAME_LEN - 1] = '\0';  // 保底封口，绝对安全

  // 4. 检查文件属性
  printf("Debug: Server is looking for file at: %s\n", m_real_file);
  fflush(stdout);
  if (stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;
  if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
  if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;

  // 5. 映射文件到内存
  int fd = open(m_real_file, O_RDONLY);
  m_file_address =
      (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);

  // 💥 优化 3：修复 mmap 致命漏洞
  if (m_file_address == MAP_FAILED) {
    m_file_address = nullptr;
    return INTERNAL_ERROR;
  }

  return FILE_REQUEST;
}

void http_conn::unmap() {
  // 💥 优化 4：严格对比 nullptr
  if (m_file_address) {
    munmap(m_file_address, m_file_stat.st_size);
    m_file_address = nullptr;  // 现代 C++ 规范，使用 nullptr 替代 0
  }
}

bool http_conn::write() {
  int temp = 0;

  // 如果没有数据需要发送，说明本次响应结束
  if (bytes_to_send == 0) {
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    init();
    return true;
  }

  while (1) {
    // 💥 核心：将内存中分散的两块数据（响应头 m_iv[0] 和 文件映射
    // m_iv[1]）一次性发给网卡
    temp = writev(m_sockfd, m_iv, m_iv_count);

    if (temp < 0) {
      // 如果 TCP 写缓冲区满了 (EAGAIN)，说明网卡太忙了
      if (errno == EAGAIN) {
        // 重新注册写事件，等网卡空闲了，epoll 会再次叫醒我们继续发
        modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
        return true;
      }
      // 如果是真的发生错误，取消内存映射并断开连接
      unmap();
      return false;
    }

    bytes_have_send += temp;
    bytes_to_send -= temp;

    // 💥 修复逻辑炸弹：严格使用 m_write_idx (响应头固定总长度) 作为判断标尺
    if (bytes_have_send >= m_write_idx) {
      // 情况 A：响应头已经全部发完，现在正在发文件内容
      m_iv[0].iov_len = 0;  // 头长度清零，不再发了
      // 偏移量 = 总已发长度 - 响应头总长度
      m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
      m_iv[1].iov_len = bytes_to_send;  // 剩下要发的全是文件
    } else {
      // 情况 B：响应头还没发完（网卡缓存极其拥堵的罕见情况）
      m_iv[0].iov_base = m_write_buf + bytes_have_send;
      m_iv[0].iov_len = m_write_idx - bytes_have_send;  // 剩余需要发送的头长度
    }

    // 数据全部发送完毕
    if (bytes_to_send <= 0) {
      unmap();  // 发送完毕，立刻释放文件的内存映射
      // 重置 epoll 监听状态为“读”
      modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

      // 如果是 Keep-Alive 长连接，重置状态机，准备迎接下一个请求
      if (m_linger) {
        init();
        return true;
      } else {
        return false;  // 短连接，直接返回 false 让外部调用 close_conn
      }
    }
  }
}

// HTTP 响应报文组装工具
bool http_conn::add_response(const char* format, ...) {
  if (m_write_idx >= WRITE_BUFFER_SIZE) return false;

  va_list arg_list;
  va_start(arg_list, format);
  // 安全的格式化写入，防止缓冲区溢出
  int len = vsnprintf(m_write_buf + m_write_idx,
                      WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

  if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
    va_end(arg_list);
    return false;
  }

  m_write_idx += len;
  va_end(arg_list);
  return true;
}

// 💥 修复编译错误：缺少指针符号 *
bool http_conn::add_status_line(int status, const char* title) {
  return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
  return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
  return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type() {
  return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger() {
  return add_response("Connection: %s\r\n",
                      (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
  return add_response("%s",
                      "\r\n");  // HTTP 协议规定，头部和正文之间必须有一个空行
}

// 💥 修复编译错误：缺少指针符号 *
bool http_conn::add_content(const char* content) {
  return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret) {
  switch (ret) {
    case INTERNAL_ERROR:
      add_status_line(500, error_500_title);
      add_headers(strlen(error_500_form));
      if (!add_content(error_500_form)) return false;
      break;
    case BAD_REQUEST:
      add_status_line(404, error_404_title);
      add_headers(strlen(error_404_form));
      if (!add_content(error_404_form)) return false;
      break;
    case FORBIDDEN_REQUEST:
      add_status_line(403, error_403_title);
      add_headers(strlen(error_403_form));
      if (!add_content(error_403_form)) return false;
      break;
    case FILE_REQUEST:
      add_status_line(200, ok_200_title);
      // 如果请求的文件有内容
      if (m_file_stat.st_size != 0) {
        add_headers(m_file_stat.st_size);
        // 组装两张发货单：第一张是响应头，第二张是文件内容
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base = m_file_address;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_count = 2;
        bytes_to_send = m_write_idx + m_file_stat.st_size;
        return true;
      } else {
        // 💥 修复空指针/空字符串隐患：如果是空文件，返回一个默认的空 HTML 页面
        const char* ok_string = "<html><body></body></html>";
        add_headers(strlen(ok_string));
        if (!add_content(ok_string)) return false;
      }
      break;
    default:
      return false;
  }

  // 如果走到这里（比如报错页面），说明只有文字内容，没有文件映射
  m_iv[0].iov_base = m_write_buf;
  m_iv[0].iov_len = m_write_idx;
  m_iv_count = 1;
  bytes_to_send = m_write_idx;
  return true;
}

// 线程池中工作线程的入口：处理整个 HTTP 生命周期
void http_conn::process() {
  // 1. 读请求并解析（状态机运转）
  HTTP_CODE read_ret = process_read();
  if (read_ret == NO_REQUEST) {
    // 如果数据还不完整，继续向 epoll 注册读事件，等待下一批数据
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    return;
  }

  // 2. 根据解析结果，生成回复报文
  bool write_ret = process_write(read_ret);
  if (!write_ret) {
    close_conn();
  }

  // 3. 准备好要发的数据后，向 epoll 注册写事件，等待网卡空闲时发出去
  modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}