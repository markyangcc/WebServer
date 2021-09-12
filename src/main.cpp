#include "../include/Server.h"
#include "../include/Util.h"

#include <cstring>
#include <fcntl.h>
#include <signal.h> //for signal
#include <sys/stat.h>
#include <thread>

std::string basePath = "."; //默认是程序当前目录
void daemon_run(); //是否以守护模式运行,守护模式qps直接翻倍，输出多影响并发

// ./webserver -t 4 -p 7000 -d（所有参数都有默认值，不带参数也可以）
// ./webserver
int main(int argc, char *argv[]) {

  int threadNumber =
      std::thread::hardware_concurrency(); //  默认线程数, C++ 11 core数
  int port = 7000;                         // 默认端口
  char tempPath[256];
  int opt;
  const char *str = "t:p:r:d";
  bool daemon = false;

  // 解析main传进来参数
  while ((opt = getopt(argc, argv, str)) != -1) {
    switch (opt) {
    case 't': {
      threadNumber = atoi(optarg);
      break;
    }
    case 'r': {
      int ret = check_base_path(optarg);
      if (ret == -1) {
        std::cout << optarg << " 目录不存在或不可访问，不可作为根目录"
                  << std::endl;

        if (getcwd(tempPath, 300) == NULL) {
          perror("getcwd error");
          basePath = ".";
        } else {
          basePath = tempPath;
        }
        break;
      }
      if (optarg[strlen(optarg) - 1] == '/') {
        optarg[strlen(optarg) - 1] = '\0';
      }
      basePath = optarg;
      break;
    }
    case 'p': {

      port = atoi(optarg);
      if (port < 0 || port > 65535) {
        std::cout << "port outof range: " << port << std::endl;
        exit(0);
      }
      break;
    }
    case 'd': {
      // 以守护进程运行（推荐）
      daemon = true;
      break;
    }

    default:
      break;
    }
  }

  // 将进程不与当前终端生命期绑定
  if (daemon)
    daemon_run();

  //  输出配置信息（守护模式与此终端脱离联系看不到输出的）
  {
    printf("---------WebServer 配置信息---------\n");
    printf("端口:\t\t%d\n", port);
    printf("线程数:\t\t%d\n", threadNumber);
    printf("根目录:\t\t%s\n", basePath.c_str());
    printf("守护模式:\t%s\n", [&daemon]() { return daemon ? "on" : "off"; }());
  }

  // 需要处理sigpipe() 信号，因为它默认会终止进程
  handle_for_sigpipe();

  HttpServer httpServer(port);
  httpServer.run(threadNumber);
}

// 守护服务进程指的是在后台运行，起到提供服务的进程。
// 1 将进程放入后台,fork（）后子进程给init收养达到目的
// 2 进程独立化 setid() 分离进程和终端
// 3 重定向标准IO描述符 open(dev/null) ,dup复制
// 守护进程编程 https://blog.csdn.net/iteye_20954/article/details/81955570

void daemon_run() {
  int pid;
  // signal(要处理的信号， 处理的方式）
  signal(SIGCHLD, SIG_IGN); // 忽略SIGCHID信号

  // 1）在父进程中，fork返回新创建子进程的进程ID；
  // 2）在子进程中，fork返回0；
  // 3）如果出现错误，fork返回一个负值；
  pid = fork();
  if (pid < 0) {
    std::cout << "fork error" << std::endl;
    exit(-1);
  }
  //父进程退出，子进程独立运行
  else if (pid > 0) {
    exit(0);
  }
  //之前parent和child运行在同一个session里,parent是会话（session）的领头进程,
  // parent进程作为会话的领头进程，如果exit结束执行的话，那么子进程会成为孤儿进程，并被init收养。
  //执行setsid()之后,child将重新获得一个新的会话(session)id。
  //这时parent退出之后,将不会影响到child了。

  setsid();
  int fd;
  fd = open("/dev/null", O_RDWR, 0); // 对stdin/stdout/stderr进行保护
  if (fd != -1) {
    // int dup2(int oldfd, int newfd);
    // 复制一个现存的文件描述符
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
  }
  if (fd > 2)
    close(fd);
}