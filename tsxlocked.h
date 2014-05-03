#include <stack>
#include <tbb/spin_mutex.h>

template<class T>
class TSXLockedStack
{
public:
    void Push(T* entry)
    {
        tbb::speculative_spin_mutex::scoped_lock guard(lock_);
        stack_.push(entry);
    }

    // For compatability with the LockFreeStack interface,
    // add an unused int parameter.
    //
    T* Pop(int)
    {
        tbb::speculative_spin_mutex::scoped_lock guard(lock_);
        if(stack_.empty())
        {
            return nullptr;
        }
        T* ret = stack_.top();
        stack_.pop();
        return ret;
    }

private:
    std::stack<T*> stack_;
    tbb::speculative_spin_mutex lock_;
};

