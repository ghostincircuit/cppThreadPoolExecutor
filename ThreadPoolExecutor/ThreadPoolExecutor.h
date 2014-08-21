#pragma once

// Local Variables:
// mode: c++
// End:

#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cassert>

typedef unsigned int u32;
class Semaphore {
public:
        inline Semaphore() : cnt(0) {}
        //~Semaphore(); use default
        inline void post() {
                std::lock_guard<std::mutex> lk(lock);
                cnt++;
                if (cnt <= 0)
                        cv.notify_one();
        }
        inline void notify_all() {
                std::lock_guard<std::mutex> lk(lock);
                if (cnt < 0) {
                        cnt = 0;
                        cv.notify_all();
                }
        }
        /*
          return true is no timeout happened, condition satisfied, we get a chance to move
          return false if timeout happened
         */
        inline bool wait(u32 sec) {
                std::unique_lock<std::mutex> lk(lock);
                cnt--;
                if (cnt < 0) {
                        if (sec != 0) {
                                auto r = cv.wait_for(lk, std::chrono::seconds(sec));
                                if (r == std::cv_status::timeout) {
                                        //we are not freed, so we free ourselves
                                        cnt++;
                                        return false;
                                }
                        } else {
                                cv.wait(lk);
                        }
                }
                return true;
        }
private:
        //a semaphore can be implemented using a condition_variable and a lock
        std::mutex lock;
        std::condition_variable cv;
        int cnt;//beging negative means some thread is waiting on the internal
                //condition_variable, otherwise(cnt>=0) means no one is waiting
};


class ThreadPoolExecutor {
public:
        typedef void (*ThreadFunction)(void *p);

        //factory method: create a thread pool with a limited concurrency
        static inline ThreadPoolExecutor *NewFixedThreadPool(u32 nThreads) {
                return new ThreadPoolExecutor(nThreads, nThreads, 0);
        }
        //factory method: create a pool with only 1 single worker thread
        static inline ThreadPoolExecutor *NewSingleThreadExecutor() {
                return new ThreadPoolExecutor(1, 1, 0);
        }
        //factory method: create a pool with unlimited concurrency
        //but usually the OS has a limit, if you add too many task
        //too quickly, you get error from OS telling you that there
        //is not enough resources
        static inline ThreadPoolExecutor *NewCachedThreadPool() {
                return new ThreadPoolExecutor(0, 0xffffffff, 60);//max
        }

        /*
          this is the constructor, usually you do no need to call this unless
          the factory methods above do not fit you

          minSize: minimum number of threads in the pool, if you do not use
          PrestartAll(), then the actual number of threads in pool may be lower
          than this value at initial stage. But the number of threads would grow
          and after a certain period of time, the number of threads would be equal
          or greater than minSize

          maxSize: the maximum number of existing threads in the pool, the number
          of threads in the pool would never execced this value.
          NOTE: you should never specify a maxSize smaller than minSize and
          maxSize must be greater than 0

          alive_sec: keepAlive value of worker thread in seconds. When a thread is
          idle for alive_sec seconds, and the current number of threads is greater
          than minSize, then the idle thread would be killed, the size of pool would
          shrink.
          NOTE: if alive_sec == 0, it means there is no timeout. Idle threads would
          always be kept alive
         */
        ThreadPoolExecutor(u32 minSize, u32 maxSize, u32 alive_sec)
                : min(minSize),
                  max(maxSize),
                  cur(0),
                  act(0),
                  atm(alive_sec),
                  qbd(false),
                  state(RUNNING) {
                          assert(maxSize != 0);
                          assert(minSize < maxSize);
                          if (minSize > maxSize)
                                  maxSize = minSize;
                          if (maxSize == 0)
                                  maxSize = 1;
                  }
        /*
          destructor, it is guranteed that when this return, all worker threads
          asoociated with this pool are dead.
         */
        ~ThreadPoolExecutor();
        /*return false when already quitting
          it is guranteed that after this call there would be at least
          min threads in the pool
        */
        bool PrestartAllMinThreads();
        /*
          return the number of worker threads in the pool
         */
        u32 GetPoolSize();
        /*
          return the minimum number of threads that should be kept in the pool
          only at initial stage would the actually number of threads be smaller
          than this number
         */
        u32 GetMinPoolSize();
        /*return false when new min is bigger than max or when pool is quitting
         */
        bool SetMinPoolSize(u32 min);
        /*
          return the maximum number of threads that can be kept in the pool
          the actual number of threads should never exceed this value
         */
        u32 GetMaxPoolSize();
        /*
          return false when new min is bigger than max or when pool is quitting
          NOTE:do not call this too often, otherwise there may be performance
          degradation
         */
        bool SetMaxPoolSize(u32 max);
        /*
          return the number of thread currently working
         */
        u32 GetActiveCount();
        /*
          return KeepAliveTime, when a thread is idle for KeepAliveTime and there
          is still no work to do and the current number of threads is larger than
          minimum value, then the thread would be killed. The pool size would shrink
         */
        u32 GetKeepAliveTime();
        /*
          alive_sec == 0 means infinite
         */
        bool SetKeepAliveTime(u32 alive_sec);
        /*
          asap(as soon as possible) measn quit even if work queue is not empty
          otherwise pool threads would quit only when current work queue is empty
          i.e. all works are done
         */
        void Shutdown(bool asap=false);
        /*
          querry whether the pool is shutdown(all worker threads quit), this does
          not gurantee that all pending works are done if you use Shutdown(true)
          to shutdown. But if you use Shutdown(false) to shutdown, it is guranteed
          that when this returns true, all works put into the threads are also done.
         */
        bool IsShutdown();
        /*
          alive_sec is the timeout for this blocking operation, 0 means forever
          this call would block if the pool is still not shutdown(RUNNING or QUITTING).
          NOTE that you can call this on a pool without first calling Shutdown() and
          this call would block until someone else called Shutdown() and everything
          is shutdown.
         */
        bool AwaitTermination(u32 alive_sec);
        /*
          fun is work function.
          use this API to add work into the threadpool request queue
         */
        bool Execute(ThreadFunction fun, void *param);
private:
        std::mutex lock;
        u32 min;//minium number of threads, may not hold true for initial stage
                //when using on demand(not calling PrestartAllMinThreads()
        u32 max;//maximum nubmer of threads, this never would never at any circumstance
                //be exceeded
        u32 cur;//current number of threads
        u32 act;//current number of threads that is working(not idle)
        u32 atm;//alive timeout in seconds
        bool qbd;//quit before all works done
        enum {RUNNING, QUITTING, DEAD} state;
        std::condition_variable quitCond;//used to implement AwaitTermination()
        Semaphore sem;//used to control thread activity
        //this struct is used to save pending work and parameter
        struct Workload {
                Workload() {}
                Workload(ThreadFunction f, void *p) : work(f), param(p) {}
                ThreadFunction work;
                void *param;
        };
        //request list
        std::list<Workload> req_q;
        //worker thread function
        static void InternalWorkerFunction(ThreadPoolExecutor *pool);
        //internally used to add one thread to threadpool
        inline void Add1Thread();
        //make sure that this is already guarded by a lock
        inline void CommonCleanup();
};
