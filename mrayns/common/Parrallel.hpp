//
// Created by wyz on 2022/2/25.
//
#pragma once
#include <thread>
#include <algorithm>
#include <optional>
#include <mutex>
#include <vector>
#include <queue>
#include <future>
#include <thread>
#include "../common/Define.hpp"

inline int actual_worker_count(int worker_count) noexcept
{
    if(worker_count <= 0)
        worker_count += static_cast<int>(std::thread::hardware_concurrency());
    return (std::max)(1, worker_count);
}

template<typename Iterable, typename Func>
void parallel_foreach(
    Iterable &&iterable, const Func &func, int worker_count = 0)
{
    std::mutex it_mutex;
    auto it = iterable.begin();
    auto end = iterable.end();
    auto next_item = [&]() -> decltype(std::make_optional(*it))
    {
      std::lock_guard lk(it_mutex);
      if(it == end)
          return std::nullopt;
      return std::make_optional(*it++);
    };

    std::mutex except_mutex;
    std::exception_ptr except_ptr = nullptr;

    worker_count = actual_worker_count(worker_count);

    auto worker_func = [&](int thread_index)
    {
      for(;;)
      {
          auto item = next_item();
          if(!item)
              break;

          try
          {
              func(thread_index, *item);
          }
          catch(...)
          {
              std::lock_guard lk(except_mutex);
              if(!except_ptr)
                  except_ptr = std::current_exception();
          }

          std::lock_guard lk(except_mutex);
          if(except_ptr)
              break;
      }
    };

    std::vector<std::thread> workers;
    for(int i = 0; i < worker_count; ++i)
        workers.emplace_back(worker_func, i);

    for(auto &w : workers)
        w.join();

    if(except_ptr)
        std::rethrow_exception(except_ptr);
}

struct ThreadPool
{
    ThreadPool(size_t);
    ~ThreadPool();

    template <typename F, typename... Args>
    auto AppendTask(F &&f, Args &&... args);
    void Wait();

  private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mut;
    std::atomic<size_t> idle;
    std::condition_variable cond;
    std::condition_variable waitCond;
    size_t nthreads;
    bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads) : idle(threads), nthreads(threads), stop(false)
{
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back([this] {
          while (true)
          {
              std::function<void()> task;
              {
                  std::unique_lock<std::mutex> lock(this->mut);
                  this->cond.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                  if (this->stop && this->tasks.empty())
                  {
                      return;
                  }
                  idle--;
                  task = std::move(this->tasks.front());
                  this->tasks.pop();
              }
              task();
              idle++;
              {
                  std::lock_guard<std::mutex> lk(this->mut);
                  if (idle.load() == this->nthreads && this->tasks.empty())
                  {
                      waitCond.notify_all();
                  }
              }
          }
        });
}

// add new work item to the pool
template <class F, class... Args> auto ThreadPool::AppendTask(F &&f, Args &&... args)
{
//    using return_type = typename InvokeResultOf<F>::type;
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mut);
        // don't allow enqueueing after stopping the pool
        if (stop)
        {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        tasks.emplace([task]() { (*task)(); });
    }
    cond.notify_one();
    return res;
}

inline void ThreadPool::Wait()
{
    std::mutex m;
    std::unique_lock<std::mutex> l(m);
    waitCond.wait(l, [this]() { return this->idle.load() == nthreads && tasks.empty(); });
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(mut);
        stop = true;
    }
    cond.notify_all();
    for (std::thread &worker : workers)
    {
        worker.join();
    }
}