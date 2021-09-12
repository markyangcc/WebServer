#include "../include/Util.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

// 实现 trim函数
std::string &trim(std::string &s) {
  if (s.empty()) {
    return s;
  }

  s.erase(0, s.find_first_not_of(" \t"));
  s.erase(s.find_last_not_of(" \t") + 1);
  return s;
}

int setnonblocking(int fd) {
  int oldop = fcntl(fd, F_GETFL);
  int newop = oldop | O_NONBLOCK;
  fcntl(fd, F_SETFL, newop);
  return oldop;
}

// 四次挥手后，如果给一个已经关闭socket发送数据，第一次会收到RST,第二次会收到SIGPIPE信号，该信号默认结束进程
void handle_for_sigpipe() { signal(SIGPIPE, SIG_IGN); }

int check_base_path(char *basePath) {
  struct stat file;
  if (stat(basePath, &file) == -1) {
    return -1;
  }
  // 不是目录 或者不可访问
  if (!S_ISDIR(file.st_mode) || access(basePath, R_OK) == -1) {
    return -1;
  }
  return 0;
}
