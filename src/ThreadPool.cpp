#include "../include/ThreadPool.h"

#include <iostream>

ThreadPool::ThreadPool(int thread_s, int max_queue_s)
    : max_queue_size(max_queue_s), thread_size(thread_s), started_(0),
      shutdown_(0) {
  if (thread_s <= 0 || thread_s > MAX_THREAD_SIZE) {
    thread_size =
        std::thread::hardware_concurrency(); // C++11, 返回 cpu支持的核心数
  }

  if (max_queue_s <= 0 || max_queue_s > MAX_QUEUE_SIZE) {
    max_queue_size = MAX_QUEUE_SIZE;
  }

  // 分配空间
  threads.resize(thread_size);

  for (int i = 0; i < thread_size; ++i) {
    // thread 只能 move 不能 copy和 assign
    threads[i] = move(std::thread(worker, this));
    threads[i].detach();
    // threads.emplace_back(worker, this); // 原地构造,但是不好detach，用 Lambda??
    // 目前没有想到分离的简便写法或许count++ 再 detach可以
    started_++;
  }

  if (started_ != 4) {
    std::cout << "inti not completed, started num: " << started_ << std::endl;
    exit(1);
  }
}

ThreadPool::~ThreadPool() {}

bool ThreadPool::append(std::shared_ptr<void> arg,
                        std::function<void(std::shared_ptr<void>)> func) {

  if (shutdown_) {
    std::cout << "ThreadPool has shutdown" << std::endl;
    return false;
  }

  std::lock_guard<std::mutex> guard(this->mtx);

  if (task_queue.size() > max_queue_size) {
    std::cout << "ThreadPool too many requests, limited to: " << max_queue_size
              << std::endl;
    return false;
  }

  ThreadTask task;
  task.arg = arg;      // httpdata
  task.process = func; // func

  task_queue.push_back(task);

  cond.notify_one(); // 唤醒一个线程线程去处理

  return true;
}

void ThreadPool::shutdown(bool graceful) {
  // lock_guard 作用域
  {
    std::lock_guard<std::mutex> guard(this->mtx);
    if (shutdown_) {
      std::cout << "ThreadPool has shutdown" << std::endl;
    }

    shutdown_ = graceful ? graceful_mode : immediate_mode; // 改为优雅关闭
    cond.notify_all();
  }

  for (int i = 0; i < thread_size; i++) {
    if (threads[i].joinable())
      threads[i].join();
  }
}

void *ThreadPool::worker(void *args) {
  ThreadPool *pool = static_cast<ThreadPool *>(args);

  // 退出线程
  if (pool == nullptr)
    return nullptr;

  // 执行线程主方法
  pool->run();
  return nullptr;
}

void ThreadPool::run() {

  while (true) {
    ThreadTask requesttask;
    {
      std::unique_lock<std::mutex> ulock(this->mtx);
      // 无任务并且未shutdown，此处循环检测
      while (task_queue.empty() && !shutdown_) {
        cond.wait(ulock);
      }

      if (task_queue.empty() &&
          ((shutdown_ == immediate_mode) || (shutdown_ == graceful_mode))) {
        break;
      }
      // FIFO
      requesttask = task_queue.front();
      task_queue.pop_front();
    }
    requesttask.process(requesttask.arg);
  }
}
