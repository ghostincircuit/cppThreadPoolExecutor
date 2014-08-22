#include "ThreadPoolExecutor.h"
#include <cassert>
//#include <iostream>

ThreadPoolExecutor::~ThreadPoolExecutor()
{
        Shutdown(true);
        AwaitTermination(dtm);
}

void ThreadPoolExecutor::Add1Thread()
{
        std::thread th(InternalWorkerFunction, this);
        th.detach();
        cur++;
}

bool ThreadPoolExecutor::PrestartAllMinThreads()
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING)
                return false;
        int diff = min - cur;
        for (auto i = 0; i < diff; i++)
                Add1Thread();
        return true;
}

u32 ThreadPoolExecutor::GetPoolSize()
{
        std::lock_guard<std::mutex> lk(lock);
        return cur;
}

u32 ThreadPoolExecutor::GetMinPoolSize()
{
        std::lock_guard<std::mutex> lk(lock);
        return min;
}

bool ThreadPoolExecutor::SetMinPoolSize(u32 amin)
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING || amin > max)
                return false;
        min = amin;
        return true;
}

u32 ThreadPoolExecutor::GetMaxPoolSize()
{
        std::lock_guard<std::mutex> lk(lock);
        return max;
}

bool ThreadPoolExecutor::SetMaxPoolSize(u32 amax)
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING || min > amax || amax == 0)
                return false;
        int diff = cur - amax;
        max = amax;
        if (diff > 0) {
                //notify extra threads to quit
                for (auto i = 0; i < diff; i++) {
                        //possible point of optimization?
                        //multi-post
                        sem.post();
                }
        } else if (diff < 0){
                //we need this to make sure that when the pool size is expanded
                //SetMaxPoolSize(), the actual number of threads would also grow
                int maxadd = amax - cur;
                int needadd = req_q.size() + act - cur;
                int toadd = (maxadd > needadd) ? needadd : maxadd;
                for (auto i = 0; i < toadd; i++)
                        Add1Thread();
        }
        return true;
}

u32 ThreadPoolExecutor::GetActiveCount()
{
        std::lock_guard<std::mutex> lk(lock);
        return act;
}

u32 ThreadPoolExecutor::GetKeepAliveTime()
{
        std::lock_guard<std::mutex> lk(lock);
        return atm;
}

bool ThreadPoolExecutor::SetKeepAliveTime(u32 alive_sec)
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING)
                return false;
        atm = alive_sec;
        /*wake up all sleeping threads
          make sure that everyone see teh new KeepAlive value
         */
        sem.notify_all();
        return true;
}

void ThreadPoolExecutor::CommonCleanup()
{//this is already guarded by a lock
        state = QUITTING;
        //make sure that every thread can get this message
        //should not use notify_all because that will not let those
        //active threads know the message, because they would be waiting
        //for semaphore value
        for (u32 i = 0; i < cur; i++)
                sem.post();
        if (cur == 0) {
                state = DEAD;
                quitCond.notify_all();
        }

}

void ThreadPoolExecutor::Shutdown(bool asap)
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING)
                return;
        qbd = asap;
        CommonCleanup();
}

bool ThreadPoolExecutor::IsShutdown()
{
        std::lock_guard<std::mutex> lk(lock);
        return state == DEAD;
}

bool ThreadPoolExecutor::AwaitTermination(u32 alive_sec)
{
        std::unique_lock<std::mutex> lk(lock);
        if (cur == 0 && state == DEAD)
                return true;
        else if (alive_sec == 0) {
                quitCond.wait(lk);
                return true;
        } else {
                auto r = quitCond.wait_for(lk, std::chrono::seconds(alive_sec));
                if (r == std::cv_status::timeout)
                        return false;
                else
                        return true;
        }
}

bool ThreadPoolExecutor::Execute(const std::function<void()>& task)
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING)
                return false;
        req_q.emplace_back(task);
        assert(cur >= act);
        u32 diff = cur - act;
        bool nmt = (diff < req_q.size());//need more threads
        if (cur < min || (nmt && cur < max)) {//lower than min or all busy
                Add1Thread();
        }
        sem.post();
        return true;
}

bool ThreadPoolExecutor::SetDestructorTimeout(u32 tm)
{
        std::lock_guard<std::mutex> lk(lock);
        if (state != RUNNING)
                return false;
        dtm = tm;
        return true;
}

void ThreadPoolExecutor::InternalWorkerFunction(ThreadPoolExecutor *self)
{
        enum {WAIT, WORK, SUICIDE} todo = WAIT;
        while (1) {
                std::function<void()> work;
                //return false means we are not freed, we timeouted
                bool timeout = !self->sem.wait(self->atm);
                {
                        std::lock_guard<std::mutex> lk(self->lock);
                        assert(self->state != DEAD);
                        /*
                          conditions for WORK:
                          1. there are some work in list
                          2. not exceeding max limit
                          3. not (quitting and qbd(quit before done))
                         */
                        /*
                          conditions for suicide
                          1. quittinad and qdb
                          2. exceeding max limit
                          3. (no work) and timeout
                         */
                        bool list_empty = self->req_q.empty();
                        bool exceed_limit = (self->cur > self->max);
                        bool quick_quit = (self->state == QUITTING) && self->qbd;
                        bool quite_idle = timeout && list_empty && self->cur > self->min;
                        bool final_quit = (self->state == QUITTING) && list_empty;
                        if (!list_empty && !exceed_limit && !quick_quit) {
                                //WORK
                                todo = WORK;
                                work = self->req_q.front();
                                self->req_q.pop_front();
                                self->act++;
                                assert(self->act != 0);
                        } else if (exceed_limit || quite_idle || quick_quit || final_quit) {
                                //SUICIDE
                                self->cur--;
                                if (self->cur == 0 && self->state == QUITTING) {
                                        self->state = DEAD;
                                        self->quitCond.notify_all();
                                }
                                todo = SUICIDE;
                        } else {
                                //WAIT
                                todo = WAIT;
                        }
                }
                if (todo == WORK) {
                        work();
                        {
                                std::lock_guard<std::mutex> lk(self->lock);
                                self->act--;
                                assert(self->act >= 0);
                        }
                } else if (todo == SUICIDE)
                        return;
                else
                        continue;
        }
}
