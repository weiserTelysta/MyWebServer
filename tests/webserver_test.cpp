#include "webserver.h"

#include <iostream>

using namespace std;

int main() {
  cout << "========== 准备启动 WebServer ==========" << endl;

  WebServer server;

  // 只需要调用 init 这一个公开接口，内部全自动装配！
  cout << "[1] 正在初始化并装配服务器组件..." << endl;
  server.init(9090, "root", "root", "webdb", 0, 0, 3, 8, 8, 0, 1);

  cout << "[2] 服务器启动成功！正在监听端口: 9090" << endl;
  cout << "========== 进入 Event Loop (按 Ctrl+C 停止) ==========" << endl;

  // 启动心脏，开始处理网络请求
  server.eventLoop();

  return 0;
}