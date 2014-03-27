/**
 * Copyright (C) 2013-2014 Uri Shamay (shamayuri@gmail.com).
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 * See http://www.gnu.org/licenses/lgpl.html .
 */
#ifndef HYBRID_LOCK_H
#define HYBRID_LOCK_H

#include <immintrin.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <sched.h>

class SpinLock
{
public:
    explicit SpinLock() : lock_(false) {}

    void lock() noexcept
    {
        while(std::atomic_exchange_explicit(&lock_, true, std::memory_order_acquire))
        {
            _mm_pause();
        }
    }

    void unlock() noexcept
    {
        std::atomic_store_explicit(&lock_, false, std::memory_order_release);
    }

    bool isLocked() const noexcept
    {
        return lock_;
    }

private:
    std::atomic<bool> lock_;
};

class HybridLock
{
public:
    explicit HybridLock(int retries = 3) : retries_(retries) {}

    void lock() noexcept
    {
        int counter = 0;
        while(1)
        {
            ++counter;
            unsigned status = _xbegin();

            if(status == _XBEGIN_STARTED)
            {
                if(!lock_.isLocked())
                {
                    ++tsx_succeed_;
                    return;
                }
                _xabort(0xff);
            }

            ++tsx_aborts_;

            if((status & _XABORT_EXPLICIT) && _XABORT_CODE(status) == 0xff && !(status & _XABORT_NESTED))
            {
                while(lock_.isLocked())
                {
                    _mm_pause();
                }
            }
            else if(!(status & _XABORT_RETRY))
            {
                break;
            }
            if(counter >= retries_)
            {
                break;
            }
        }

        //fallback
        lock_.lock();
    }

    void unlock() noexcept
    {
        lock_.isLocked() ? lock_.unlock() : _xend();
    }

    size_t getTSXAborted() const noexcept
    {
        return tsx_aborts_;
    }

    size_t getTSXSucceed() const noexcept
    {
        return tsx_succeed_;
    }

private:
    int retries_;
    SpinLock lock_;
    std::atomic<std::size_t> tsx_aborts_;
    std::atomic<std::size_t> tsx_succeed_;
};

#endif
