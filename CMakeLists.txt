cmake_minimum_required(VERSION 3.10)
project(MyWebServer)

# 告诉编译器去哪里找头文件
include_directories(threadpool)
include_directories(SYNMySql)
include_directories(http)
include_directories(log)
include_directories(timer)
include_directories(webserver)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

# ---------------------------------------------------------
# 1. 真正的服务器可执行文件
# ---------------------------------------------------------
add_executable(server_run 
    main.cpp 
    threadpool/threadpool.cpp
)
target_link_libraries(server_run Threads::Threads)

# ---------------------------------------------------------
# 2. 线程池测试程序
# ---------------------------------------------------------
add_executable(test_pool 
    tests/threadpool_test.cpp 
    threadpool/threadpool.cpp
)
target_link_libraries(test_pool Threads::Threads)

# ---------------------------------------------------------
# 3. 数据库连接池测试程序
# ---------------------------------------------------------
add_executable(test_mysql_pool 
    tests/mysql_pool_test.cpp 
    SYNMySql/mysql_connection.cpp
)
target_link_libraries(test_mysql_pool Threads::Threads mysqlclient)

# ---------------------------------------------------------
# 4. HTTP连接类测试程序
# ---------------------------------------------------------
add_executable(test_http_conn 
    tests/http_conn_test.cpp 
    http/http_conn.cpp
    SYNMySql/mysql_connection.cpp
)
target_link_libraries(test_http_conn Threads::Threads mysqlclient)

# ---------------------------------------------------------
# 5. 日志系统测试程序
# ---------------------------------------------------------
add_executable(test_log 
    tests/log_test.cpp 
    log/log.cpp
)
# log 里用到了 std::thread，所以必须链接 Threads::Threads
target_link_libraries(test_log Threads::Threads)

# ---------------------------------------------------------
# 6. 定时器系统测试程序
# ---------------------------------------------------------
add_executable(test_timer 
    tests/timer_test.cpp 
    timer/timer.cpp      # 替换为你的 timer 源文件实际路径
    http/http_conn.cpp   # 因为 timer 依赖了 http_conn::m_user_count
    log/log.cpp          # 因为 http_conn 可能依赖了 log
    SYNMySql/mysql_connection.cpp
)
# 链接多线程库
target_link_libraries(test_timer Threads::Threads mysqlclient)

# ---------------------------------------------------------
# 7. WebServer 综合集成测试程序
# ---------------------------------------------------------
add_executable(test_webserver 
    tests/webserver_test.cpp     # 待会儿要写的测试主程序
    webserver/webserver.cpp      # 核心大管家
    http/http_conn.cpp           # HTTP 解析
    timer/timer.cpp              # 定时器
    log/log.cpp                  # 日志
    SYNMySql/mysql_connection.cpp # 数据库连接池
    threadpool/threadpool.cpp    # 线程池
)
# WebServer 是个庞然大物，需要链接多线程和 MySQL 库
target_link_libraries(test_webserver Threads::Threads mysqlclient)