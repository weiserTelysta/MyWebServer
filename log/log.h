#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>

#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "block_queue.hpp"

using namespace std;

class Log {
 public:
  // 迈耶斯单例
  static Log* get_instance() {
    static Log instance;
    return &instance;
  }

  // 原版的tinywebserver使用了全局变量
  // 公开获取状态函数
  int get_close_log() { return m_close_log; }

  // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
  bool init(const char* file_name, int close_log, int log_buf_size = 8192,
            int split_lines = 5000000, int max_queue_size = 0);

  void write_log(int level, const char* format, ...);

  void flush(void);

 private:
  Log();
  ~Log();

  // 异步写日志的核心工作函数
  void async_write_log() {
    string single_log;

    // 当析构函数触发时，能安全退出这个死循环
    while (!m_is_stop && m_log_queue->pop(single_log)) {
      // 使用 RAII 机制的 lock_guard 自动加锁解锁
      std::lock_guard<std::mutex> lock(m_mutex);
      fputs(single_log.c_str(), m_fp);
    }
  }

 private:
  std::string dir_name;  // 路径名
  std::string log_name;  // log文件名
  int m_split_lines;     // 日志最大行数
  int m_log_buf_size;    // 日志缓冲区大小
  long long m_count;     // 日志行数记录
  int m_today;           // 因为按天分类,记录当前时间是那一天
  FILE* m_fp;            // 打开log的文件指针
  char* m_buf;
  block_queue<string>* m_log_queue;  // 阻塞队列
  bool m_is_async;                   // 是否同步标志位

  std::mutex m_mutex;
  std::thread* m_write_thread;  // 管理后台异步写入的线程指针
  bool m_is_stop;               // 控制后台线程安全退出的标志位

  int m_close_log;  // 关闭日志
};

// 宏定义保持不变
#define LOG_DEBUG(format, ...)                                \
  if (0 == Log::get_instance()->get_close_log()) {            \
    Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
    Log::get_instance()->flush();                             \
  }
#define LOG_INFO(format, ...)                                 \
  if (0 == Log::get_instance()->get_close_log()) {            \
    Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
    Log::get_instance()->flush();                             \
  }
#define LOG_WARN(format, ...)                                 \
  if (0 == Log::get_instance()->get_close_log()) {            \
    Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
    Log::get_instance()->flush();                             \
  }
#define LOG_ERROR(format, ...)                                \
  if (0 == Log::get_instance()->get_close_log()) {            \
    Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
    Log::get_instance()->flush();                             \
  }

#endif