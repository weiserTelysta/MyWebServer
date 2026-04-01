#ifndef MY_SQL_CONNECTION_POOL_H
#define MY_SQL_CONNECTION_POOL_H

#include<mysql/mysql.h>
#include<string>
#include<mutex>
#include<queue>
#include <condition_variable>

class connection_pool {
public:
    //单例模式获取实例。
    static connection_pool* GetInstance();

    //数据库初始化，单例模式创建后可以直接填写。
    void init(std::string url, std::string User, std::string PassWord, 
              std::string DataBaseName, int Port, int MaxConn, int close_log);

    //核心接口
    //获取数据库连接
    MYSQL* GetConnection();  
    //释放连接
    bool ReleaseConnection(MYSQL* conn);
    // 获取当前剩余连接数。
    int GetFreeConn();
    // 销毁连接
    void DestroyPool();

private:
    //私有化构造和析构函数.
    connection_pool();
    ~connection_pool();

    // 禁用拷贝构造和赋值操作,确保单例绝对唯一)
    connection_pool(const connection_pool&) = delete;
    connection_pool& operator=(const connection_pool&) = delete;

private:
    int m_maxConn;
    int m_curConn; 
    int m_freeConn;  
    
    //线程池
    std::queue<MYSQL*> m_connQueue;
    
    // 锁
    std::mutex m_mutex;
    std::condition_variable m_cond;


    // 数据库基本信息
    std::string m_url;          // 主机地址
    std::string m_Port;         // 数据库端口号
    std::string m_User;         // 登陆数据库用户名
    std::string m_PassWord;     // 登陆数据库密码
    std::string m_DatabaseName; // 使用数据库名
    int m_close_log;            // 日志开关
};

class ConnectionGuard {
public:
    // 构造时自动获取连接
    ConnectionGuard(MYSQL** con, connection_pool* connPool);
    // 析构时自动释放连接
    ~ConnectionGuard();
    
private:
    MYSQL* m_conRAII;
    connection_pool* m_poolRAII;
};


#endif