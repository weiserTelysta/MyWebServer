#include "mysql_connection.h"
#include <iostream>

// 构建函数
connection_pool::connection_pool(){
    m_curConn = 0;
    m_freeConn = 0;
    m_maxConn = 0;
}

// 析构函数
connection_pool::~connection_pool(){
    DestroyPool();
}

// 获取单例
connection_pool* connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}

//初始化连接池
void connection_pool::init(std::string url, std::string User, std::string PassWord,std::string DBName, int Port, int MaxConn, int close_log) {
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for(int i = 0; i < MaxConn; i++){
        MYSQL *con = nullptr;
        con = mysql_init(con);

        if(con == nullptr){
            std::cerr << "MySQL Error: Initialize Failed!" << std::endl;
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, nullptr, 0);
        
        if(con == nullptr){
            std::cerr << "MySQL Error: Connect Failed!" << std::endl;
            exit(1);
        }
    
        m_connQueue.push(con);
        m_freeConn++;
    }
    
    m_maxConn = m_freeConn;
}

// 获取连接
MYSQL* connection_pool::GetConnection(){
    // 初始化是否完成
    if(m_maxConn == 0 ){
        return nullptr;
    }

    std::unique_lock<std::mutex> lock(m_mutex);

    //队列是否为空，否则睡眠。
    m_cond.wait(lock,[this]{
        return !m_connQueue.empty();
    });

    MYSQL *conn = m_connQueue.front();
    m_connQueue.pop();
    m_curConn++;
    m_freeConn--;

    return conn;
}

// 释放连接池
bool connection_pool::ReleaseConnection(MYSQL* conn){
    if (conn == nullptr) return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connQueue.push(conn);
        m_curConn--;
        m_freeConn++;
    }

    m_cond.notify_one();
    return true;
}

// 获取剩余连接数
int connection_pool::GetFreeConn(){
    return m_freeConn;
}

// 销毁连接
void connection_pool::DestroyPool(){
    std::lock_guard<std::mutex> lock(m_mutex);

    while(!m_connQueue.empty()){
        MYSQL* conn = m_connQueue.front();
        m_connQueue.pop();

        mysql_close(conn);
    }

    m_curConn = 0;
    m_freeConn = 0;
    m_maxConn = 0;
}


// ==========================================
// RAII 资源守卫类实现：借用即获取，析构即归还
// ==========================================
ConnectionGuard::ConnectionGuard(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();
    
    m_conRAII = *SQL;
    m_poolRAII = connPool;
}

ConnectionGuard::~ConnectionGuard() {
    if (m_conRAII && m_poolRAII) {
        m_poolRAII->ReleaseConnection(m_conRAII);
    }
}