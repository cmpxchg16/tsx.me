#include <mutex>
#include <stack>

template<class T>
class LockedStack
{
public:
    void Push(T* entry)
    {
        std::lock_guard<std::mutex> guard(lock_);
        stack_.push(entry);
    }

    // For compatability with the LockFreeStack interface,
    // add an unused int parameter.
    //
    T* Pop(int)
    {
        std::lock_guard<std::mutex> guard(lock_);
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
    std::mutex lock_;
};

