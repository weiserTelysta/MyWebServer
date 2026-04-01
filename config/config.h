#ifndef CONFIG_H
#define CONFIG_H

#include <string>

class Config {
 public:
  Config();
  ~Config() = default;

  void parse_arg(int argc, char* argv[]);

  // 基础配置
  int PORT;
  int LOGWrite;
  int TRIGMode;
  int LISTENTrigmode;
  int CONNTrigmode;
  int OPT_LINGER;
  int sql_num;
  int thread_num;
  int close_log;
  int actor_model;

  // 🚀 新增：数据库相关配置 (适配 Docker)
  std::string sql_host;
  int sql_port;
  std::string sql_user;
  std::string sql_pwd;
  std::string sql_db;
};

#endif