#include <stack>
#include <unistd.h>
#include <atomic>

class SpinLock {

public:
    SpinLock() : state_(Unlocked) {}

    void lock()
    {
        do
        {
            while (state_.load(std::memory_order_acquire) == Locked)
                usleep(250);
        }
        while(state_.exchange(Locked, std::memory_order_acquire) == Locked);
    }

    void unlock()
    {
        state_.store(Unlocked, std::memory_order_release);
    }

private:
    typedef enum {Locked, Unlocked} LockState;
    std::atomic<LockState> state_;
};

template<class T>
class SpinLockedStack
{
public:
    void Push(T* entry)
    {
        std::lock_guard<SpinLock> guard(lock_);
        stack_.push(entry);
    }

    // For compatability with the LockFreeStack interface,
    // add an unused int parameter.
    //
    T* Pop(int)
    {
        std::lock_guard<SpinLock> guard(lock_);
        if(stack_.empty())
        {
            return nullptr;
        }
        T* ret = stack_.top();
        stack_.pop();
        return ret;
    }

private:
    SpinLock lock_;
    std::stack<T*> stack_;
};

