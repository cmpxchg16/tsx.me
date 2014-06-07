#define MAX_THREADS 32

#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <sys/time.h>
#include <cassert>

#include <atomic>
#include <mutex>
#include <thread>

#include <tbb/spin_mutex.h>

#include "locks.h"


class MutexCounter
{
public:
    void increment()
    {
        std::lock_guard<std::mutex> guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    std::mutex lock_;
    size_t counter_ = 0;
};

class STMCounter
{
public:
    void increment()
    {
        __transaction_atomic
        {
            ++counter_;
        }
    }

    size_t count()
    {
        return counter_;
    }

private:
    size_t counter_ = 0;
};

class TSXCounter
{
public:
    void increment()
    {
        tbb::speculative_spin_mutex::scoped_lock guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    tbb::speculative_spin_mutex lock_;
    size_t counter_ = 0;
};

class NaiveSpinCounter
{
public:
    void increment()
    {
        std::lock_guard<NaiveSpinLock> guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    NaiveSpinLock lock_;
    size_t counter_ = 0;
};

class PthreadSpinCounter
{
public:
    void increment()
    {
        std::lock_guard<PthreadSpinLock> guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    PthreadSpinLock lock_;
    size_t counter_ = 0;
};

class CLHCounter
{
public:
    void increment()
    {
        std::lock_guard<CLHLock> guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    CLHLock lock_;
    size_t counter_ = 0;
};

class MPSCCounter
{
public:
    void increment()
    {
        std::lock_guard<MPSCLock> guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    MPSCLock lock_;
    size_t counter_ = 0;
};

class TicketCounter
{
public:
    void increment()
    {
        std::lock_guard<TicketLock> guard(lock_);
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    TicketLock lock_;
    size_t counter_ = 0;
};

class AtomicCounter
{
public:
    AtomicCounter() : counter_(0) {}

    void increment()
    {
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    std::atomic<size_t> counter_;
};

class DirtyCounter
{
public:
    void increment()
    {
        ++counter_;
    }

    size_t count()
    {
        return counter_;
    }

private:
    size_t counter_ = 0;
};

std::atomic<bool> running;

template<class Counter>
void Worker(Counter& counter)
{
    while(!running.load(std::memory_order_acquire)) {}
    while(running.load(std::memory_order_acquire))
        counter.increment();
}

template<class Counter>
double Test(int nthreads)
{
    const int test_time = 5;
    const int test_iterations = 5;

    size_t total = 0;
    for(int it = 0; it < test_iterations; it++)
    {
        Counter counter;
        std::thread threads[MAX_THREADS];

        for(int i = 0; i < nthreads; i++)
        {
            threads[i] = std::thread(Worker<Counter>, std::ref(counter));
        }

        running.store(true, std::memory_order_release);
        sleep(test_time);
        running.store(false, std::memory_order_release);

        for(int i = 0; i < nthreads; i++)
        {
            threads[i].join();
        }
        total += counter.count();
    }
    return (double)total / (test_time * test_iterations);
}

int main()
{
    for(int i=1; i<=MAX_THREADS; i++)
    {
        double t1 = Test<DirtyCounter>(i);
        double t2 = Test<AtomicCounter>(i);
        double t3 = Test<PthreadSpinCounter>(i);
        double t4 = Test<CLHCounter>(i);
        double t5 = Test<MPSCCounter>(i);
        double t6 = Test<TicketCounter>(i);
        double t7 = Test<NaiveSpinCounter>(i);
        double t8 = Test<TSXCounter>(i);
        double t9 = Test<STMCounter>(i);
        double t10 = Test<MutexCounter>(i);
        printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", i, (int)t1, (int)t2, (int)t3, (int)t4, (int)t5, (int)t6, (int)t7, (int)t8, (int)t9, (int)t10);
    }
    return 0;
}
