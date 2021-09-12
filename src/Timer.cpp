#include "../include/Timer.h"
#include "../include/Epoll.h"

#include <mutex>
#include <sys/time.h>

size_t TimerNode::current_msec = 0; // 当前时间

const size_t TimerManager::DEFAULT_TIME_OUT = 20 * 1000; // 20s

TimerNode::TimerNode(std::shared_ptr<HttpData> httpData, size_t timeout)
    : deleted_(false), httpData_(httpData) {
  current_time();
  expiredTime_ = current_msec + timeout;
}

TimerNode::~TimerNode() {

  // httpData 要先删除掉，否则存在shared_ptr引用,释放不掉
  // 采用的标记删除，deleted状态不用处理，reset之后变为null了，不会进入if
  // 要析构TImeNode,所以这里实际判断的是 expired状态的
  if (httpData_) {
    auto it = Epoll::httpDataMap.find(httpData_->clientSocket_->fd);
    if (it != Epoll::httpDataMap.end()) {
      Epoll::httpDataMap.erase(it);
    }
  }
}

inline void TimerNode::current_time() {
  struct timeval cur;
  gettimeofday(&cur, NULL);
  current_msec = (cur.tv_sec * 1000) + (cur.tv_usec / 1000);
}

void TimerNode::deleted() {
  // 删除采用标记删除， 并及时析构 HttpData，以关闭描述符
  // 关闭定时器时应该把 httpDataMap 里的 HttpData 一起erase
  httpData_.reset(); // reset 释放空间
  deleted_ = true;
}

void TimerManager::addTimer(std::shared_ptr<HttpData> httpData,
                            size_t timeout) {
  Shared_TimerNode timerNode(new TimerNode(httpData, timeout));
  {
    std::lock_guard<std::mutex> guard(lock_);
    TimerQueue.push(timerNode);
    // 关联 TimerNode和 HttpData
    httpData->setTimer(timerNode);
  }
}

void TimerManager::handle_expired_event() {
  std::lock_guard<std::mutex> guard(lock_);
  // 更新当前时间
  TimerNode::current_time();

  // 定时循环处理超时连接
  while (!TimerQueue.empty()) {
    Shared_TimerNode timerNode = TimerQueue.top();
    if (timerNode->isDeleted()) {
      // 删除节点
      TimerQueue.pop();
    } else if (timerNode->isExpire()) {
      // 过期直接删除了，不采用标记删除了
      // timerNode->deleted();
      TimerQueue.pop();
    } else {
      break;
    }
  }
}
