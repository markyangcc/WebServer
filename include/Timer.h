#pragma once

#include "HttpData.h"
#include <mutex>

#include <deque>
#include <memory>
#include <queue>

class HttpData;

class TimerNode {
public:
  TimerNode(std::shared_ptr<HttpData> httpData, size_t timeout);
  ~TimerNode();

public:
  bool isDeleted() const { return deleted_; }

  size_t getExpireTime() { return expiredTime_; }

  bool isExpire() {
    // 频繁调用系统调用不好,所以放在调用代码 while
    // 循环前面调用一次，所以对时间控制没那么严格
    // current_time();
    return expiredTime_ < current_msec;
  }

  void deleted();

  std::shared_ptr<HttpData> getHttpData() { return httpData_; }

  static void current_time();

  static size_t current_msec; // 当前时间

private:
  bool deleted_;
  size_t expiredTime_; // 毫秒
  std::shared_ptr<HttpData> httpData_;
};

struct TimerCmp {
  bool operator()(std::shared_ptr<TimerNode> &a,
                  std::shared_ptr<TimerNode> &b) const {
    return a->getExpireTime() > b->getExpireTime();
  }
};

class TimerManager {
public:
  typedef std::shared_ptr<TimerNode> Shared_TimerNode;

public:
  void addTimer(std::shared_ptr<HttpData> httpData, size_t timeout);

  void handle_expired_event();

  const static size_t DEFAULT_TIME_OUT;

private:
  std::priority_queue<Shared_TimerNode, std::deque<Shared_TimerNode>, TimerCmp>
      TimerQueue;
  std::mutex lock_;
};

