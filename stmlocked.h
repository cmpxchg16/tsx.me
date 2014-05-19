namespace std __attribute__ ((__visibility__ ("default")))
{
  __attribute__((transaction_pure))
  void
  __throw_bad_alloc(void) __attribute__((__noreturn__));
}

template<class T>
class STMLockedStack
{
public:
    void Push(T* entry)
    {
	__transaction_atomic
	{
           stack_.push(entry);
	}
    }

    // For compatability with the LockFreeStack interface,
    // add an unused int parameter.
    //
    T* Pop(int)
    {
	T* ret = nullptr;
	__transaction_atomic
	{
        	if(!stack_.empty())
		{
        		ret = stack_.top();
        		stack_.pop();
		}
	}
        return ret;
    }

private:
    std::stack<T*> stack_;
};
