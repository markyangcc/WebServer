#pragma once

#include <functional>
#include <list>
#include <vector>

#include <condition_variable>
#include <mutex>
#include <thread>

const int MAX_THREAD_SIZE = 4;
const int MAX_QUEUE_SIZE = 1024;

typedef enum { immediate_mode = 1, graceful_mode = 2 } ShutdownMode;

struct ThreadTask {
  // 实际应该是 HttpData对象
  std::shared_ptr<void> arg;
  // 实际传入的是 do_request;
  std::function<void(std::shared_ptr<void>)> process;
};

class ThreadPool {
public:
  ThreadPool(int thread_s, int max_queue_s);
  ~ThreadPool();

  bool append(std::shared_ptr<void> arg,
              std::function<void(std::shared_ptr<void>)> fun);
  void shutdown(bool graceful);

private:
  static void *worker(void *args);

  void run();

private:
  // 线程同步互斥, mtx 在 cond 前面
  std::mutex mtx;
  std::condition_variable cond;

  // 线程池属性
  int thread_size;
  int max_queue_size;
  int started_;  // 计数
  int shutdown_; // 状态
  std::vector<std::thread> threads;
  std::list<ThreadTask> task_queue; // 任务队列
};
