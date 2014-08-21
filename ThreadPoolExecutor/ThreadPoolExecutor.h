#pragma once

// Local Variables:
// mode: c++
// End:

#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

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
        std::mutex lock;
        std::condition_variable cv;
        int cnt;
};


class ThreadPoolExecutor {
public:
        typedef void (*ThreadFunction)(void *p);
        //factory methods
        static inline ThreadPoolExecutor *NewFixedThreadPool(u32 nThreads) {
                return new ThreadPoolExecutor(nThreads, nThreads, 0);
        }
        static inline ThreadPoolExecutor *NewSingleThreadExecutor() {
                return new ThreadPoolExecutor(1, 1, 0);
        }
        static inline ThreadPoolExecutor *NewCachedThreadPool() {
                return new ThreadPoolExecutor(0, 0xffffffff, 60);//max
        }

        ThreadPoolExecutor(u32 minSize, u32 maxSize, u32 alive_sec)
                : min(minSize),
                  max(maxSize),
                  cur(0),
                  act(0),
                  atm(alive_sec),
                  qbd(false),
                  state(RUNNING)
                {}
        ~ThreadPoolExecutor();
        /*return false when already quitting
          it is guranteed that after this call there would be at least
          min threads in the pool
        */
        bool PrestartAllMinThreads();
        //void PrestartMinThread();
        u32 GetPoolSize();
        u32 GetMinPoolSize();
        /*return false when new min is bigger than max or when pool is quitting
         */
        bool SetMinPoolSize(u32 min);
        u32 GetMaxPoolSize();
        /*return false when new min is bigger than max or when pool is quitting
          NOTE:do not call this too often, otherwise there may be performance
          degradation
         */
        bool SetMaxPoolSize(u32 max);
        u32 GetActiveCount();
        u32 GetKeepAliveTime();
        /*
          0 means infinite
         */
        bool SetKeepAliveTime(u32 alive_sec);
        /*
          asap(as soon as possible) measn quit even if work queue is not empty
         */
        void Shutdown(bool asap=false);
        //void ShutdownNow();
        bool IsShutdown();
        bool AwaitTermination(u32 alive_sec);
        bool Execute(ThreadFunction fun, void *param);
private:
        std::mutex lock;
        u32 min;
        u32 max;
        u32 cur;
        u32 act;//current active
        u32 atm;//alive timeout in seconds
        bool qbd;//quit before all works done
        enum {RUNNING, QUITTING, DEAD} state;
        std::condition_variable quitCond;
        Semaphore sem;

        struct Workload {
/*
                Workload(ThreadFunction f, void *p)
                : work(f), param(p) {}
*/
                Workload() {}
                Workload(ThreadFunction f, void *p) : work(f), param(p) {}
                ThreadFunction work;
                void *param;
        };
        std::list<Workload> req_q;
        static void InternalWorkerFunction(ThreadPoolExecutor *pool);
        inline void Add1Thread();
        inline void CommonCleanup();
};
