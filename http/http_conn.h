#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <string>
#include <mutex>

#include "mysql_connection.h"
// #include "lst_timer.h"
// #include "log.h"

class http_conn{
public:
    static constexpr int FILENAME_LEN = 200;
    static constexpr int READ_BUFFER_SIZE = 2048;
    static constexpr int WRITE_BUFFER_SIZE = 1024;

    enum METHOD{
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    enum HTTP_CODE{
        NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST,
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
    };

    enum LINE_STATUS{
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn(){};
    ~http_conn(){};

public:
    void init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, std::string user, std::string passwd, std::string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address() { return &m_address; }

    //初始化数据库用户表
    void initmysql_result(connection_pool *connPool);

    //Reactor / Proactor 模式状态标志
    int timer_flag;
    int improv;
    int m_state; //读为0 写为1

public:
    //全局共享的epoll句柄和用户总数
    static int m_epollfd;
    static int m_user_count;

    // 将用户缓存与锁封装为静态成员，供所有连接对象安全共享。
    static std::map<std::string, std::string> m_users;
    static std::mutex m_user_mutex;

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

private:
    int m_sockfd;
    sockaddr_in m_address;

    // 缓冲区相关
    char m_read_buf[READ_BUFFER_SIZE];
    long m_read_idx;
    long m_checked_idx;
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    // 状态机相关
    CHECK_STATE m_check_state;
    METHOD m_method;

    // HTTP请求解析结果
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;

    // 文件映射相关
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;

    // 业务逻辑相关
    int cgi;
    char *m_string;
    int bytes_to_send;
    int bytes_have_send;
    char *doc_root;

    // 数据库连接配置信息
    int m_TRIGMode;
    int m_close_log;
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
