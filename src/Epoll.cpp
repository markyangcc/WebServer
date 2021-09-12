#include "../include/Epoll.h"
#include "../include/Util.h"
#include <sys/epoll.h>
#include <vector>

std::unordered_map<int, std::shared_ptr<HttpData>> Epoll::httpDataMap;

const int Epoll::MAX_EVENTS = 10000;

struct epoll_event *Epoll::events;

// 可读 | ET模式 | 保证一个socket连接在任一时刻只被一个线程处理
// 操作系统最多触发其上注册的一个事件，且只触发一次，
// 除非我们使用epoll_ctl函数重置该文件描述符上注册的 EPOLLONESHOT 事件。
// 这样，在一个线程使用socket时，其他线程无法操作socket

const __uint32_t Epoll::DEFAULT_EVENTS = (EPOLLIN | EPOLLET | EPOLLONESHOT);

TimerManager Epoll::timerManager;

int Epoll::init(int max_events) {
  int epoll_fd = ::epoll_create(max_events);
  if (epoll_fd == -1) {
    std::cout << "epoll_create error" << std::endl;
    exit(-1);
  }
  events = new epoll_event[max_events];
  return epoll_fd;
}

int Epoll::addfd(int epoll_fd, int fd, __uint32_t events,
                 std::shared_ptr<HttpData> httpData) {
  struct epoll_event event;
  event.events = (EPOLLIN | EPOLLET);
  event.data.fd = fd;
  httpDataMap[fd] = httpData;

  int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
  if (ret < 0) {
    std::cout << "epoll add error" << std::endl;
    // 释放httpData
    httpDataMap[fd].reset();
    return -1;
  }
  return 0;
}

int Epoll::modfd(int epoll_fd, int fd, __uint32_t events,
                 std::shared_ptr<HttpData> httpData) {
  struct epoll_event event;
  event.events = events;
  event.data.fd = fd;
  httpDataMap[fd] = httpData;

  int ret = ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
  if (ret < 0) {
    std::cout << "epoll mod error" << std::endl;
    // 释放httpData
    httpDataMap[fd].reset();
    return -1;
  }
  return 0;
}

int Epoll::delfd(int epoll_fd, int fd, __uint32_t events) {
  struct epoll_event event;
  event.events = events;
  event.data.fd = fd;
  int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event);
  if (ret < 0) {
    std::cout << "epoll del error" << std::endl;
    return -1;
  }
  auto it = httpDataMap.find(fd);
  if (it != httpDataMap.end()) {
    httpDataMap.erase(it);
  }
  return 0;
}

// listenfd 新的连接
void Epoll::handleConnection(const ServerSocket &serverSocket) {

  std::shared_ptr<ClientSocket> newClient(new ClientSocket);
  // epoll 是ET模式，循环接收连接
  // 需要将listen_fd设置为non-blocking

  while (serverSocket.accept(*newClient) > 0) {
    // 设置 non-blocking, 因为ET
    int ret = setnonblocking(newClient->fd);
    if (ret < 0) {
      std::cout << "setnonblocking error" << std::endl;
      newClient->close();
      continue;
    }

    // TODO:限制并发， accept 然后立即close

    std::shared_ptr<HttpData> sharedHttpData(new HttpData);
    sharedHttpData->request_ = std::shared_ptr<HttpRequest>(new HttpRequest());
    sharedHttpData->response_ =
        std::shared_ptr<HttpResponse>(new HttpResponse());

    std::shared_ptr<ClientSocket> sharedClientSocket(new ClientSocket());
    // swap 避免 copy
    sharedClientSocket.swap(newClient);
    sharedHttpData->clientSocket_ = sharedClientSocket;
    sharedHttpData->epoll_fd = serverSocket.epoll_fd;

    // EPOLLIN | EPOLLET | EPOLLONESHOT 只被一个进程处理
    addfd(serverSocket.epoll_fd, sharedClientSocket->fd,
          EPOLLIN | EPOLLET | EPOLLONESHOT, sharedHttpData);
    // 设置定时器默认时间，keepalive 就继续加 DEFAULT_TIME_OUT
    timerManager.addTimer(sharedHttpData, TimerManager::DEFAULT_TIME_OUT);
  }
}

std::vector<std::shared_ptr<HttpData>>
Epoll::poll(const ServerSocket &serverSocket, int max_event, int timeout) {

  int nfds = epoll_wait(serverSocket.epoll_fd, events, max_event, timeout);

  if (nfds == -1) {
    // epoll_wait 被信号中断的错误，忽略还是终止？ 目前采取忽略中断的方式
    // 比如 ctrl + z, 挂起 webserver进程，接着通过jobs + fg %<num> 命令
    // 调回前台，就会收到 EINTR信号

    if (errno == EINTR) {
      perror("EINTE");
    } else {
      perror("epoll_wait error");
      exit(-1);
    }
  }

  // epoll ET + non-blocking 核心逻辑处理
  std::vector<std::shared_ptr<HttpData>> httpDatas;
  // 遍历events集合
  for (int i = 0; i < nfds; i++) {
    int fd = events[i].data.fd;

    if (fd == serverSocket.listen_fd) {
      // 防止可能有的假触发
      if (events[i].events & EPOLLIN)
        handleConnection(serverSocket);
    }
    // 出错的描述符，移除定时器， 关闭文件描述符
    // //增加 EPOLLRDHUP事件，表示对端断开连接
    else if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLRDHUP) ||
             (events[i].events & EPOLLHUP)) {

      auto it = httpDataMap.find(fd);

      if (it != httpDataMap.end()) {
        // 将 HttpData节点和 TimerNode的关联分开，这样 HttpData会立即析构，
        // 在析构函数内关闭文件描述符等资源, 不用在这里 earse
        it->second->closeTimer();
      } else {
        continue;
      }
    }
    //如果是可读事件
    else if (events[i].events & EPOLLIN) {

      auto it = httpDataMap.find(fd);
      if (it != httpDataMap.end()) {
        // EPOLLPRI 紧急数据事件
        if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLPRI)) {
          httpDatas.push_back(it->second);
          // 清除定时器 HttpData.closeTimer()
          it->second->closeTimer();
          httpDataMap.erase(it);
        }
      } else {
        std::cout << "长连接第二次连接未找到" << std::endl;
        ::close(fd);
        continue;
      }
      // 这里有个问题是 TimerNode正常超时释放时
    }
  }
  return httpDatas;
}
