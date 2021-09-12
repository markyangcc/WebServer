#pragma once

#include <arpa/inet.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class ClientSocket {

public:
  ClientSocket() { fd = -1; };
  ~ClientSocket();
  void close();

  socklen_t mLen;
  sockaddr_in mAddr;
  int fd;
};

void setReusePort(int fd);

class ServerSocket {
public:
  ServerSocket(int port = 8080, const char *ip = nullptr);
  ~ServerSocket();

  void bind();
  void listen();
  void close();
  int accept(ClientSocket &) const;

public:
  sockaddr_in mAddr;
  int listen_fd;
  int epoll_fd;
  int mPort;
  const char *mIp;
};
