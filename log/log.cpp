#include "log.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <chrono>  // C++11 时间库

using namespace std;

Log::Log()
    : m_count(0),
      m_is_async(false),
      m_is_stop(false),
      m_write_thread(nullptr),
      m_log_queue(nullptr),
      m_buf(nullptr),
      m_fp(nullptr) {}

Log::~Log() {
  m_is_stop = true;  // 告诉后台线程准备下班了

  // 退出异步写线程，防止内存泄漏和段错误
  if (m_is_async && m_write_thread != nullptr) {
    // 唤醒可能正在等待条件变量的 pop()
    m_log_queue->push("");
    if (m_write_thread->joinable()) {
      m_write_thread->join();
    }
    delete m_write_thread;
    delete m_log_queue;
  }

  if (m_fp != NULL) {
    fclose(m_fp);
  }
  if (m_buf != NULL) {
    delete[] m_buf;
  }
}

bool Log::init(const char* file_name, int close_log, int log_buf_size,
               int split_lines, int max_queue_size) {
  if (max_queue_size >= 1) {
    m_is_async = true;
    m_log_queue = new block_queue<string>(max_queue_size);

    m_write_thread = new std::thread([this]() { this->async_write_log(); });
  }

  m_close_log = close_log;
  m_log_buf_size = log_buf_size;
  m_buf = new char[m_log_buf_size];
  memset(m_buf, '\0', m_log_buf_size);
  m_split_lines = split_lines;

  // 使用线程安全的 localtime_r 获取当前时间
  time_t t = time(NULL);
  struct tm my_tm;
  localtime_r(&t, &my_tm);

  // 使用 std::string 的现代截取和拼接，放弃TinyWebServer的C风格
  std::string file_str(file_name);
  std::string log_full_name;
  size_t p = file_str.find_last_of('/');

  if (p == std::string::npos) {
    dir_name = "./";  // 如果没有传路径，默认当前目录
    log_name = file_str;
  } else {
    dir_name = file_str.substr(0, p + 1);
    log_name = file_str.substr(p + 1);
  }

  char time_buf[64] = {0};
  snprintf(time_buf, sizeof(time_buf), "%d_%02d_%02d_", my_tm.tm_year + 1900,
           my_tm.tm_mon + 1, my_tm.tm_mday);

  log_full_name = dir_name + time_buf + log_name;

  m_today = my_tm.tm_mday;

  // fopen 只认 C 格式字符串，所以要调用 .c_str()
  m_fp = fopen(log_full_name.c_str(), "a");
  if (m_fp == NULL) {
    return false;
  }
  return true;
}

void Log::write_log(int level, const char* format, ...) {
  auto now = std::chrono::system_clock::now();
  time_t t = std::chrono::system_clock::to_time_t(now);
  auto now_usec = std::chrono::duration_cast<std::chrono::microseconds>(
                      now.time_since_epoch())
                      .count() %
                  1000000;

  struct tm my_tm;
  localtime_r(&t, &my_tm);  // 线程安全

  const char* s = "";
  switch (level) {
    case 0:
      s = "[debug]:";
      break;
    case 1:
      s = "[info]:";
      break;
    case 2:
      s = "[warn]:";
      break;
    case 3:
      s = "[erro]:";
      break;
    default:
      s = "[info]:";
      break;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_count++;

    if (m_today != my_tm.tm_mday ||
        m_count % m_split_lines == 0)  // 日志翻页逻辑
    {
      fflush(m_fp);
      fclose(m_fp);
      char tail[32] = {0};

      snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900,
               my_tm.tm_mon + 1, my_tm.tm_mday);

      std::string new_log;

      if (m_today != my_tm.tm_mday) {
        new_log = dir_name + tail + log_name;
        m_today = my_tm.tm_mday;
        m_count = 0;
      } else {
        new_log = dir_name + tail + log_name + "." +
                  std::to_string(m_count / m_split_lines);
      }
      m_fp = fopen(new_log.c_str(), "a");
    }
  }

  va_list valst;
  va_start(valst, format);

  string log_str;
  {
    std::lock_guard<std::mutex> lock(m_mutex);

    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now_usec, s);

    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
  }  // m_mutex 自动解锁

  va_end(valst);

  if (m_is_async && !m_log_queue->full()) {
    m_log_queue->push(log_str);
  } else {
    std::lock_guard<std::mutex> lock(m_mutex);
    fputs(log_str.c_str(), m_fp);
  }
}

void Log::flush(void) {
  std::lock_guard<std::mutex> lock(m_mutex);
  fflush(m_fp);
}