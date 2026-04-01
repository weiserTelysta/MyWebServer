#include "config.h"

#include <stdlib.h>
#include <unistd.h>

#include <iostream>

Config::Config() {
  // 基础配置默认值
  PORT = 9006;
  LOGWrite = 0;
  TRIGMode = 0;
  LISTENTrigmode = 0;
  CONNTrigmode = 0;
  OPT_LINGER = 0;
  sql_num = 8;
  thread_num = 8;
  close_log = 0;
  actor_model = 0;

  // 🚀 核心改造：优先从环境变量读取数据库配置
  // 这样在 Docker Compose 中配置环境变量，C++ 程序就能直接获取
  const char* env_sql_host = std::getenv("MYSQL_HOST");
  const char* env_sql_port = std::getenv("MYSQL_PORT");
  const char* env_sql_user = std::getenv("MYSQL_USER");
  const char* env_sql_pwd = std::getenv("MYSQL_PASSWORD");
  const char* env_sql_db = std::getenv("MYSQL_DATABASE");

  // 如果环境变量存在则使用，否则使用默认值（适配你的 docker-compose 映射）
  sql_host = env_sql_host ? env_sql_host : "127.0.0.1";
  sql_port = env_sql_port ? std::atoi(env_sql_port)
                          : 3307;  // 注意你的 docker-compose 映射的是 3307
  sql_user = env_sql_user ? env_sql_user : "root";
  sql_pwd = env_sql_pwd ? env_sql_pwd : "root";  // 你的 compose 里密码是 root
  sql_db = env_sql_db ? env_sql_db : "webdb";    // 你的 compose 里库名是 webdb
}

void Config::parse_arg(int argc, char* argv[]) {
  int opt;
  // 保留原有的命令行解析，作为第二种配置手段
  const char* str = "p:l:m:o:s:t:c:a:";
  while ((opt = getopt(argc, argv, str)) != -1) {
    switch (opt) {
      case 'p':
        PORT = atoi(optarg);
        break;
      case 'l':
        LOGWrite = atoi(optarg);
        break;
      case 'm':
        TRIGMode = atoi(optarg);
        break;
      case 'o':
        OPT_LINGER = atoi(optarg);
        break;
      case 's':
        sql_num = atoi(optarg);
        break;
      case 't':
        thread_num = atoi(optarg);
        break;
      case 'c':
        close_log = atoi(optarg);
        break;
      case 'a':
        actor_model = atoi(optarg);
        break;
      default:
        break;
    }
  }
}