cmake_minimum_required(VERSION 3.10)
project(MyWebServer)

# 告诉编译器去哪里找头文件
include_directories(threadpool)
include_directories(SYNMySql)
include_directories(http)

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