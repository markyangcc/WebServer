#include "../include/Socket.h"
#include "../include/Util.h"

#include <cstring>
#include <sys/socket.h>

void setReuseAddrPort(int fd) {
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void *)&opt, sizeof(opt));
}

ServerSocket::ServerSocket(int port, const char *ip) : mPort(port), mIp(ip) {
  bzero(&mAddr, sizeof(mAddr));
  mAddr.sin_family = AF_INET;
  mAddr.sin_port = htons(port);

  // 传进来的ip为空，则默认监听所有ip
  if (ip != nullptr) {
    ::inet_pton(AF_INET, ip, &mAddr.sin_addr);
  } else {
    mAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  }

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (listen_fd == -1) {
    std::cout << "creat socket error in file <" << __FILE__ << "> "
              << "at " << __LINE__ << std::endl;
    exit(0);
  }

  // TCP状态位于 TIME_WAIT ，可以重用端口
  setReuseAddrPort(listen_fd);
  // ET 模式下需要设置非阻塞
  setnonblocking(listen_fd);
}

void ServerSocket::bind() {

  int ret = ::bind(listen_fd, (struct sockaddr *)&mAddr, sizeof(mAddr));
  if (ret == -1) {
    std::cout << "bind error in file <" << __FILE__ << "> "
              << "at " << __LINE__ << std::endl;
    exit(0);
  }
}

void ServerSocket::listen() {
  // 第二个是 baklog值，
  // 如果 baklog值小于 somaxconn,baklog值会被更新为
  // somaxconn堆（内核有一个if判断比较大小，然后赋值）
  // 全连接队列的长度是baklog的值（if判断之后更新的backlog值，所以想增大全连接队列长度，需要同时增大两个参数的值）
  // 半连接队列长度 = min(backlog,somaxconn,tcp_max_syn_backlog) + 1,
  // 再向上取整到2的下一个幂次方，最小不能小于 16 somaxconn:4096
  // tcp_max_syn_backlog:128

  int ret = ::listen(listen_fd, 2048);
  if (ret == -1) {
    std::cout << "listen error in file <" << __FILE__ << "> "
              << "at " << __LINE__ << std::endl;
    exit(0);
  }
}

int ServerSocket::accept(ClientSocket &clientSocket) const {

  int clientfd = ::accept(listen_fd, NULL, NULL);

  // 写数据时，若一次发送的数据超过TCP发送缓冲区，则返EAGAIN/EWOULDBLOCK，表示数据没用发送完

  if (clientfd < 0) {
    if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
      return clientfd;
    else {
      std::cout << "accept error in file <" << __FILE__ << "> "
                << "at " << __LINE__ << std::endl;
      std::cout << "clientfd:" << clientfd << std::endl;
      // exit(0);
    }
  }
  clientSocket.fd = clientfd;
  return clientfd;
}

void ServerSocket::close() {
  if (listen_fd >= 0) {
    ::close(listen_fd);
    std::cout << "定时器超时，关闭文件描述符:" << listen_fd << std::endl;
    listen_fd = -1;
  }
}

ServerSocket::~ServerSocket() { close(); }

void ClientSocket::close() {
  if (fd >= 0 && fd <= 2) {
    std::cout << "Unknow Error: " << fd << std::endl;
  } else {
    std::cout << "对端断开连接: " << fd << std::endl;
    ::close(fd);
    fd = -1;
  }
}

ClientSocket::~ClientSocket() { close(); }
