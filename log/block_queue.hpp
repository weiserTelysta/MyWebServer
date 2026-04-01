#ifndef BLOCK_QUEUE_HPP
#define BLOCK_QUEUE_HPP

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>

template <class T>
class block_queue {
 public:
  // 构造函数：初始化 vector 大小
  explicit block_queue(int max_size = 1000)
      : m_max_size(max_size), m_size(0), m_front(-1), m_back(-1) {
    if (max_size <= 0) {
      exit(-1);
    }
    m_array.resize(max_size);
  }

  // 析构函数：由于使用了 std::vector，内存会自动释放，不需要手动 delete [] 了
  ~block_queue() = default;

  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_size = 0;
    m_front = -1;
    m_back = -1;
  }

  // 判断队列是否满了
  bool full() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size >= m_max_size;
  }

  // 判断队列是否为空
  bool empty() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return 0 == m_size;
  }

  // 返回队首元素
  bool front(T& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (0 == m_size) {
      return false;
    }
    value = m_array[m_front];
    return true;
  }

  // 返回队尾元素
  bool back(T& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (0 == m_size) {
      return false;
    }
    value = m_array[m_back];
    return true;
  }

  int size() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size;
  }

  int max_size() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_max_size;
  }

  // 往队列添加元素
  bool push(const T& item) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_size >= m_max_size) {
      //   m_cond.notify_all(); // 满了应该不需要惊群吧。
      return false;
    }

    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size++;

    m_cond.notify_one();
    return true;
  }

  // pop时, 如果当前队列没有元素, 将会等待条件变量
  bool pop(T& item) {
    std::unique_lock<std::mutex> lock(m_mutex);

    // 使用 Lambda 表达式处理虚假唤醒，取代原来的 while(m_size <= 0)
    m_cond.wait(lock, [this]() { return m_size > 0; });

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;

    return true;
  }

  // 增加了超时处理的 pop
  bool pop(T& item, int ms_timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);

    // 放弃复杂的 gettimeofday，使用 C++11 的 chrono 和 wait_for
    if (!m_cond.wait_for(lock, std::chrono::milliseconds(ms_timeout),
                         [this]() { return m_size > 0; })) {
      // 如果返回 false，说明超时且条件(m_size > 0)仍不满足
      return false;
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;

    return true;
  }

 private:
  std::mutex m_mutex;
  std::condition_variable m_cond;

  std::vector<T> m_array;
  int m_size;
  int m_max_size;
  int m_front;
  int m_back;
};

#endif