#pragma once

#include "HttpParse.h"
#include "HttpResponse.h"
#include "Socket.h"
#include "Timer.h"

class TimerNode;

class HttpData : public std::enable_shared_from_this<HttpData> {
public:
  HttpData() : epoll_fd(-1) {}

public:
  void closeTimer();
  void setTimer(std::shared_ptr<TimerNode>);

public:
  int epoll_fd;
  std::shared_ptr<HttpRequest> request_;
  std::shared_ptr<HttpResponse> response_;
  std::shared_ptr<ClientSocket> clientSocket_;

private:
  std::weak_ptr<TimerNode> timer_;
};
