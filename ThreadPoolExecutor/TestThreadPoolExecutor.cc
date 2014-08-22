#include <iostream>
#include <thread>
#include <cassert>
#include <functional>
using namespace std;

#include "ThreadPoolExecutor.h"

inline void sleep_sec(int sec)
{
        std::this_thread::sleep_for(std::chrono::seconds(sec));
}

inline void sleep_sec(float sec)
{
        int msec = sec * 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(msec));
}

template<int sec = 2>
void print_task(int par)
{
        sleep_sec(sec);
        cout << "work done: " << par << endl;
}

struct para {
        std::mutex *lock;
        int *num;
};
template<int sec = 4>
void adder_task(void *par)
{
        para *p = (para *)par;
        sleep_sec(sec);
        p->lock->lock();
        (*(p->num))++;
        p->lock->unlock();
}

void test_sem1()
{
        cout << "========================== test_sem1() ==================" << endl;
        Semaphore s;
        s.post();
        cout << 1 << endl;

        s.post();
        cout << 2 << endl;

        assert(s.wait(1) == true);
        cout << 3 << endl;

        assert(s.wait(1) == true);
        cout << 4 << endl;

        assert(s.wait(1) == false);
        cout << 5 << endl;

        assert(s.wait(1) == false);
        cout << 6 << endl;

        s.post();
        cout << 7 << endl;

        assert(s.wait(1) == true);
        cout << 8 << endl;
}

void test_sem2()
{
        cout << "====================== test_sem2() =====================" << endl;
        auto func1 =
                [] (Semaphore *s, std::mutex *lk, int *g) {
                s->wait(1);
                lk->lock();
                (*g)++;
                lk->unlock();
        };
        Semaphore s;
        std::mutex lk;
        int g = 0;
        for (auto i = 0; i < 8; i++) {
                std::thread th(func1, &s, &lk, &g);
                th.detach();
        }
        for (auto i = 0; i < 8; i++)
                s.post();
        sleep_sec(4);
        assert(g == 8);
}

void test_sem3()
{
        cout << "========================== test_sem3() =================" << endl;
        auto func =
                [] (Semaphore *s, std::mutex *lk, int *g) {
                s->wait(0);
                lk->lock();
                (*g)++;
                lk->unlock();
        };
        Semaphore s;
        std::mutex lk;
        int g = 0;
        for (auto i = 0; i < 12; i++) {
                std::thread th(func, &s, &lk, &g);
                th.detach();
        }
        for (auto i = 0; i < 9; i+=3) {
                s.post();
                s.post();
                s.post();
                sleep_sec(3);
                assert(g == i+3);
        }
        for (auto i = 0; i < 12; i++)
                s.post();
        sleep_sec(2);
}

void test_init1()
{//no creation
        cout << "============================ " << __func__ << " ==============" << endl;
        std::mutex lk;
        auto sonf =
                [] (std::mutex *plk) {
                sleep_sec(2);
                auto r = plk->try_lock();
                assert(r == false);
                return;
        };

        auto pool = new ThreadPoolExecutor(2, 2, 0);
        std::thread son(sonf, &lk);
        delete pool;
        lk.lock();
        son.join();
        lk.unlock();
}

void test_init2()
{//prestart create
        cout << "============================ " << __func__ << " ==============" << endl;
        std::mutex lk;
        auto sonf =
                [] (std::mutex *plk) {
                sleep_sec(2);
                auto r = plk->try_lock();
                assert(r == false);
                return;
        };

        std::thread son(sonf, &lk);
        auto pool = new ThreadPoolExecutor(4, 8, 0);
        pool->PrestartAllMinThreads();
        assert(pool->GetPoolSize() == 4);
        delete pool;
        lk.lock();
        son.join();
        lk.unlock();
}

void test_init3()
{//no creation, with some timeout
        cout << "============================ " << __func__ << " ==============" << endl;
        std::mutex lk;
        auto sonf =
                [] (std::mutex *plk) {
                sleep_sec(2);
                auto r = plk->try_lock();
                assert(r == false);
                return;
        };

        std::thread son(sonf, &lk);
        auto pool = new ThreadPoolExecutor(4, 8, 10);
        pool->PrestartAllMinThreads();
        assert(pool->GetPoolSize() == 4);
        delete pool;
        lk.lock();
        son.join();
        lk.unlock();
}

void test_run1()
{//one work one work one work, non-asap quit
        cout << "============================ " << __func__ << " ==============" << endl;
        auto func =
                [] (int *par) {
                int *np = (int *)par;
                sleep_sec(1);
                (*np)++;
                cout << "work" << endl;
                return;
        };

        auto pool = new ThreadPoolExecutor(4, 8, 0);
        //pool->PrestartAllMinThreads();

        int num = 0;
        for (auto i = 0; i < 4; i++) {
                pool->Execute(std::bind(func, &num));
                sleep_sec(2);
        }
        pool->Shutdown(false);//non-asap quit
        delete pool;
        assert(num == 4);
}

void test_run1_1()
{//try asap quit
        cout << "============================ " << __func__ << " ==============" << endl;
        std::mutex lk;
        auto func =
                [&] (int *par) {
                int *np = (int *)par;
                sleep_sec(8);
                lk.lock();
                (*np)++;
                lk.unlock();
                cout << "work" << endl;
                return;
        };

        auto pool = new ThreadPoolExecutor(0, 2, 0);

        int num = 0;
        for (auto i = 0; i < 4; i++) {
                pool->Execute(std::bind(func, &num));
        }
        auto r = pool->GetPoolSize();
        assert(r == 2);
        sleep_sec(1);
        r = pool->GetActiveCount();
        assert(r == 2);
        pool->Shutdown(true);//asap quit
        delete pool;
        assert(num == 2);
}

void test_run2()
{//more work than can process, stack up works
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(2, 4, 0);
        pool->PrestartAllMinThreads();

        para pa[8];
        std::mutex mux;
        int val = 0;
        for (auto i = 0; i < 8; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                pa[i].lock = &mux;
                pa[i].num = &val;
                pool->Execute(std::bind(&adder_task<8>, (void *)&pa[i]));
                cout << "add work: " << i << endl;
        }
        delete pool;
        assert(val < 8);
}

void test_run2_1()
{//more work than can process, stack up works
 //non-asap approach
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(2, 4, 0);
        pool->PrestartAllMinThreads();

        para pa[8];
        std::mutex mux;
        int val = 0;
        for (auto i = 0; i < 8; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                pa[i].lock = &mux;
                pa[i].num = &val;
                pool->Execute(std::bind(&adder_task<4>, (void *)&pa[i]));
                cout << "add work: " << i << endl;
        }

        pool->Shutdown(false);
        assert(pool->IsShutdown() == false);
        pool->AwaitTermination(0);
        assert(pool->IsShutdown() == true);
        assert(val == 8);
        delete pool;
}

void test_run3()
{//multiple work adder
        cout << "============================ " << __func__ << " ==============" << endl;
        para pa[8];
        para pb[8];
        std::mutex glock;
        int val = 0;
        float f = 0.01;
        auto pool = new ThreadPoolExecutor(4, 8, 0);
        auto adder0 =
                [&] () {
                for (auto i = 0; i < 8; i++) {
                        pa[i].lock = &glock;
                        pa[i].num = &val;
                        sleep_sec(f);
                        pool->Execute(std::bind(&adder_task<6>, &pa[i]));
                }
        };
        auto adder1 =
                [&] () {
                for (auto i = 1; i < 8; i++) {
                        pb[i].lock = &glock;
                        pb[i].num = &val;
                        sleep_sec(f);
                        pool->Execute(std::bind(&adder_task<6>, &pb[i]));
                }
        };
        thread s0(adder0);
        thread s1(adder1);
        s0.join();
        s1.join();
        pool->Shutdown(true);
        assert(pool->IsShutdown() == false);
        pool->AwaitTermination(0);
        assert(pool->IsShutdown() == true);
        assert(val < 16);
        delete pool;
}

void test_run3_1()
{//multiple work adder
 //use soft ending approach
        cout << "============================ " << __func__ << " ==============" << endl;
        para pa[8];
        para pb[8];
        std::mutex glock;
        int val = 0;
        float f = 0.01;
        auto pool = new ThreadPoolExecutor(4, 8, 0);
        auto adder0 =
                [&] () {
                for (auto i = 0; i < 8; i++) {
                        pa[i].lock = &glock;
                        pa[i].num = &val;
                        sleep_sec(f);
                        pool->Execute(std::bind(&adder_task<6>, &pa[i]));
                }
        };
        auto adder1 =
                [&] () {
                for (auto i = 0; i < 8; i++) {
                        pb[i].lock = &glock;
                        pb[i].num = &val;
                        sleep_sec(f);
                        pool->Execute(std::bind(&adder_task<6>, &pb[i]));
                }
        };
        thread s0(adder0);
        thread s1(adder1);
        s0.join();
        s1.join();
        pool->Shutdown(false);
        assert(pool->IsShutdown() == false);
        pool->AwaitTermination(0);
        assert(pool->IsShutdown() == true);
        assert(val == 16);
        delete pool;
}

void test_run4()
{//on demand behavior
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(4, 8, 0);
        assert(pool->GetPoolSize() == 0);
        auto lam = [&] () {pool->Execute(std::bind(&print_task<4>, 0));};

        lam();
        assert(pool->GetPoolSize() == 1);

        lam();
        assert(pool->GetPoolSize() == 2);

        lam();
        assert(pool->GetPoolSize() == 3);

        lam();
        assert(pool->GetPoolSize() == 4);

        lam();
        assert(pool->GetPoolSize() == 5);

        sleep_sec(6);
        assert(pool->GetPoolSize() == 5);

        lam();
        assert(pool->GetPoolSize() == 5);

        lam();
        assert(pool->GetPoolSize() == 5);

        delete pool;
}

void test_SetMaxPoolSize1()
{//set max pool size to be lower
 //no work
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(4, 8, 0);
        pool->PrestartAllMinThreads();
        assert(pool->GetPoolSize() == 4);
        pool->SetMinPoolSize(0);
        sleep_sec(1);
        assert(pool->GetPoolSize() == 4);
        pool->SetMaxPoolSize(1);
        sleep_sec(2);//give threads time to quit
        assert(pool->GetPoolSize() == 1);
        delete pool;
}

void test_SetMaxPoolSize2()
{
//set max pool size to be lower
//with work
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(2, 4, 0);
        for (int i = 0; i < 6; i++) {
                pool->Execute(std::bind(&print_task<4>, i));
        }

        assert(pool->GetPoolSize() == 4);
        pool->SetMinPoolSize(0);
        sleep_sec(1);
        assert(pool->GetPoolSize() == 4);
        pool->SetMaxPoolSize(1);
        sleep_sec(5);//give active threads time to finish current time
        assert(pool->GetActiveCount() < 2);
        assert(pool->GetPoolSize() == 1);
        delete pool;
}

void test_SetMaxPoolSize3()
{
//set max pool size randomly
//with work
        cout << "============================ " << __func__ << " ==============" << endl;
        para param;
        std::mutex glock;
        int val = 0;
        float f = 0.25;
        auto pool = new ThreadPoolExecutor(0, 4, 0);
        param.lock = &glock;
        param.num = &val;
        auto adder0 =
                [&] () {
                for (auto i = 0; i < 32; i++) {
                        sleep_sec(f);
                        pool->Execute(std::bind(&adder_task<1>, &param));
                }
        };
        auto adder1 =
                [&] () {
                for (auto i = 0; i < 32; i++) {
                        sleep_sec(f);
                        pool->Execute(std::bind(&adder_task<1>, &param));
                }
        };
        thread s0(adder0);
        thread s1(adder1);
        sleep_sec(4);
        assert(pool->GetPoolSize() == 4);
        cout << "set max to 8" << endl;
        pool->SetMaxPoolSize(8);
        assert(pool->GetPoolSize() == 8);
        pool->SetMaxPoolSize(2);
        sleep_sec(2);
        assert(pool->GetPoolSize() == 2);
        sleep_sec(2);
        pool->SetMaxPoolSize(7);
        s0.join();
        s1.join();
        pool->Shutdown(false);
        assert(pool->IsShutdown() == false);
        pool->AwaitTermination(0);
        assert(pool->IsShutdown() == true);
        assert(val == 64);
        delete pool;
}

void test_SetKeepAlive1()
{
//check if pool can really shrink
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(0, 4, 1);
        pool->PrestartAllMinThreads();
        for (auto i = 0; i < 4; i++)
                pool->Execute(std::bind(&print_task<1>, i));
        auto r = 0;
        r = pool->GetPoolSize();
        assert(r == 4);
        sleep_sec(4);
        r = pool->GetPoolSize();
        assert(r == 0);
        delete pool;
}

void test_SetKeepAlive2()
{
//check that set new value works for all
        cout << "============================ " << __func__ << " ==============" << endl;
        auto pool = new ThreadPoolExecutor(0, 4, 2);
        pool->PrestartAllMinThreads();
        for (auto i = 0; i < 4; i++)
                pool->Execute(std::bind(&print_task<1>, i));
        auto r = 0;
        r = pool->GetPoolSize();
        assert(r == 4);
        pool->SetKeepAliveTime(0);//infinite
        sleep_sec(4);
        r = pool->GetPoolSize();
        assert(r == 4);
        pool->SetKeepAliveTime(1);//1 second
        float f = 3.5;
        sleep_sec(f);
        r = pool->GetPoolSize();
        assert(r == 0);
        delete pool;
}

void test_factories()
{
        auto single = ThreadPoolExecutor::NewSingleThreadExecutor();
        auto fixed = ThreadPoolExecutor::NewFixedThreadPool(16);
        auto unlimited = ThreadPoolExecutor::NewCachedThreadPool();
        auto task =
                [] (int pa) {
                sleep_sec(1);
                cout << pa << endl;
                return;
        };
        for (int i = 0; i < 8; i++)
                single->Execute(std::bind(task, i));
        for (long int i = 0; i < 32; i++) {
                fixed->Execute(std::bind(task, i));
                unlimited->Execute(std::bind(task, i));
        }
        single->Shutdown(false);
        fixed->Shutdown(false);
        unlimited->Shutdown(false);
        while(1) {
                auto r1 = single->IsShutdown();
                auto r2 = fixed->IsShutdown();
                auto r3 = unlimited->IsShutdown();
                cout << r1 << " " << r2 << " " << r3 << endl;
                if (r1 && r2 && r3)
                        break;
                else
  
                      sleep_sec(2);
        }
        delete single;
        delete fixed;
        delete unlimited;
}

void test_fuck()
{
        //auto unlimited = ThreadPoolExecutor::NewCachedThreadPool();
        auto unlimited = ThreadPoolExecutor::NewFixedThreadPool(128);
        std::mutex gl;
        int val = 0;
        const int N = 1024*16*64;
        auto task =
                [&] () {
                gl.lock();
                val++;
                gl.unlock();
                return;
        };
        for (long int i = 0; i < N; i++) {
                unlimited->Execute(task);
        }
        unlimited->Shutdown(false);
        delete unlimited;
        assert(val == N);
}

void test_functional()
{
        struct Obj {
                Obj(int a) : m(a) {}
                int m;
                void mf1(int a0) {
                        cout << a0 << endl;
                }
                void mf2() {
                        cout << m << endl;
                }
        };
        Obj obj(345);
        auto unlimited = ThreadPoolExecutor::NewFixedThreadPool(2);
        for (int i = 0; i < 4; i++) {
                std::function<void()> fu =
                        std::bind(&Obj::mf1, &obj, i);
                unlimited->Execute(fu);
                const float f = 0.001;
                sleep_sec(f);
        }
        std::function<void()> fu = std::bind(&Obj::mf2, &obj);
        for (auto i = 0; i < 4; i++)
                unlimited->Execute(fu);
        
        unlimited->Shutdown(false);
        delete unlimited;
}

int tmain()
{
        for (auto i = 0; i < 32; i++) {
                test_sem1();
                test_sem2();
                test_sem3();


                test_init1();
                test_init2();
                test_init3();

                test_run1();
                test_run1_1();
                test_run2();
                test_run2_1();
                test_run4();
                test_run3();
                test_run3_1();


                test_SetMaxPoolSize1();
                test_SetMaxPoolSize2();

                test_SetMaxPoolSize3();
                test_SetKeepAlive1();

                test_SetKeepAlive2();

                test_factories();


                test_fuck();
                test_functional();
        }
        cout << "PASS" << endl<< "PASS" << endl<< "PASS" << endl<< "PASS" << endl;
        return 0;
}
