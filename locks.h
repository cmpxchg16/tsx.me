/******************************************************************************
 * Copyright (c) 2014, Pedro Ramalhete, Andreia Correia, Uri Shamay
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Concurrency Freaks nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/
 
#include <pthread.h>
#include <atomic>
#include <thread>


/**********************************************************************/
class NaiveSpinLock
{
public:
    void lock()
    {
        while (true)
        {
            if (!lock_.test_and_set(std::memory_order_acquire))
            {
                return;
            }
        }
    }

    void unlock()
    {
        lock_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
};
/**********************************************************************/
class PthreadSpinLock
{
public:

    PthreadSpinLock()
    {
        pthread_spin_init(&lock_, 0);
    }

    void lock()
    {
        pthread_spin_lock(&lock_);
    }

    void unlock()
    {
        pthread_spin_unlock(&lock_);
    }

private:
    pthread_spinlock_t lock_;
};
/**********************************************************************/
/*
 * This is a C11 implementation of the CLH lock where the previous node is removed.
 * See slide 45 here:
 * http://www.cs.rice.edu/~vs3/comp422/lecture-notes/comp422-lec19-s08-v1.pdf
 *
 * Notice that this lock is NOT recursive, but it is easy to make it recursive.
 *
 * This mutual exclusion lock is "Starvation-Free", assuming that the run-time
 * can provide that kind of guarantee.
 *
 * It is possible to remove the field "mynode" from clh_mutex_node_t, but
 * then we need to use either a thread-local variables to store them, or we
 * need to return it from clh_mutex_lock() and pass it to
 * clh_mutex_unlock() as argument, which is ok for some usages, but hard to
 * do for others.
 * In C++1x, it should be easy to use this technique with a "Guard" pattern
 * and then pass the operation on the object associated with the mutex through
 * a lambda.
 * The advantage of this last approach is that there are no loads, stores, or
 * false-sharing for "mynode"  because the compiler can put those variables on
 * a register, or at worse, on the current thread's stack.
 *
 * This locking mechanism has some similarities with the MCS lock because each
 * thread is spinning on the "islocked" of the previous node, which means that
 * each thread is waiting on a different cache-line, thus reducing
 * synchronization.
 * http://www.cise.ufl.edu/tr/DOC/REP-1992-71.pdf
 * The main different from the MCS lock (section 2) is that MCS may have to do
 * an Compare-And-Swap in addition to the atomic_store() in the unlock function.
 *
 * The CLH lock uses the (theoretically) minimum amount of synchronization
 * operations to perform a mutual exclusion lock, because:
 * - In the lock() function it has a single synchronized operation
 *   atomic_exchange() with one acquire and one release barrier.
 * - In the unlock() function is has a single synchronized operation, the
 *   atomic_store() with a release barrier.
 */
class CLHLock
{
	struct clh_mutex_node_
	{
		std::atomic<char> succ_must_wait;
	};
	
	typedef struct clh_mutex_node_ clh_mutex_node_t;

	typedef struct
	{
		clh_mutex_node_t * mynode;
		char padding[64];  // To avoid false sharing with the tail
		std::atomic<clh_mutex_node_t *> tail;
	} clh_mutex_t;
	
	static clh_mutex_node_t * clh_mutex_create_node(char islocked)
	{
		clh_mutex_node_t * new_node = (clh_mutex_node_t *)malloc(sizeof(clh_mutex_node_t));
		new_node->succ_must_wait.store(islocked, std::memory_order_relaxed);
		return new_node;
	}
	
public:

	CLHLock()
	{
		// We create the first sentinel node unlocked, with islocked=0
		clh_mutex_node_t * node = clh_mutex_create_node(0);
		lock_.mynode = node;
		lock_.tail.store(node);
	}
	
	~CLHLock()
	{
		free(lock_.tail.load());
	}
	
	/*
	 * Locks the mutex for the current thread. Will wait for other threads
	 * that did the atomic_exchange() before this one.
	 *
	 * Progress Condition: Blocking
	 */
	void lock()
	{
		// Create the new node locked by default, setting islocked=1
		clh_mutex_node_t *mynode = clh_mutex_create_node(1);
		clh_mutex_node_t *prev = lock_.tail.exchange(mynode);

		// This thread's node is now in the queue, so wait until it is its turn
		char prev_islocked = prev->succ_must_wait.load(std::memory_order_relaxed);
		if (prev_islocked) {
			while (prev_islocked) {
				std::this_thread::yield();
				prev_islocked = prev->succ_must_wait.load();
			}
		}
		// This thread has acquired the lock on the mutex and it is now safe to
		// cleanup the memory of the previous node.
		free(prev);

		// Store mynode for clh_mutex_unlock() to use. We could replace
		// this with a thread-local, not sure which is faster.
		lock_.mynode = mynode;
	}
	
	/*
	 * Unlocks the mutex. Assumes that the current thread holds the lock on the
	 * mutex.
	 *
	 * Progress Condition: Wait-Free Population Oblivious
	 */
	void unlock()
	{
		// We assume that if this function was called, it is because this thread is
		// currently holding the lock, which means that lock_.mynode is pointing to
		// the current thread's mynode.
		if (lock_.mynode == NULL) {
			// ERROR: This will occur if unlock() is called without a lock()
			return;
		}
		lock_.mynode->succ_must_wait.store(0);
	}
	
private:
	clh_mutex_t lock_;
};
/**********************************************************************/
/*
 * A mutual exclusion lock that uses the MPSC queue invented by Dmitry Vyukov.
 * This is actually a simple variant of the CLH lock which was discovered
 * independently by Travis Craig at the University of Washington
 * (UW TR 93-02-02, February 1993), and by Anders Landin and Eric Hagersten
 * of the Swedish Institute of Computer Science (IPPS, 1994).
 * http://www.cs.rochester.edu/research/synchronization/pseudocode/ss.html#clh
 *
 * Notice that this lock is NOT recursive.
 * This mutual exclusion lock is "Starvation-Free", assuming that the run-time
 * can provide that kind of guarantee.
 */
class MPSCLock
{
	struct mpsc_mutex_node_
	{
		std::atomic<mpsc_mutex_node_ *> next;
	};
	
	typedef struct mpsc_mutex_node_ mpsc_mutex_node_t;

	typedef struct
	{
		std::atomic<mpsc_mutex_node_t *> head;
		char padding[64];                     // To avoid false sharing with the head and tail
		std::atomic<mpsc_mutex_node_t *> tail;
	} mpsc_mutex_t;
	
	static mpsc_mutex_node_t * mpsc_mutex_create_node(void)
	{
		mpsc_mutex_node_t * new_node = (mpsc_mutex_node_t *)malloc(sizeof(mpsc_mutex_node_t));
		return new_node;
	}
	
public:

	MPSCLock()
	{
		mpsc_mutex_node_t * node = mpsc_mutex_create_node();
		lock_.head.store(node);
		lock_.tail.store(node);
	}
	
	~MPSCLock()
	{
	    // TODO: check if head and tail are equal (no other thread waiting on the lock)
		free(lock_.head.load());
	}
	
	/*
	 * 1. A thread wishing to acquire the lock starts by creating a new node that
	 *    it will insert in the tail of the queue, using an atomic_exchange() on
	 *    the tail.
	 * 2. The atomic_exchange() operation will return a pointer to the previous
	 *    node to which tail was pointing, and now this thread can set the "next"
	 *    of that node to point to its own node, using an atomic_store().
	 * 3. We now loop until the head reaches the node previous to our own.
	 */
	void lock()
	{
		mpsc_mutex_node_t *mynode = mpsc_mutex_create_node();
		mpsc_mutex_node_t *prev = lock_.tail.exchange(mynode);
		prev->next.store(mynode);

		// This thread's node is now in the queue, so wait until it is its turn
		mpsc_mutex_node_t * lhead = lock_.head.load();
		while (lhead != prev) {
			std::this_thread::yield();
			lhead = lock_.head.load();
		}
		// This thread has acquired the lock on the mutex
	}
	
	/*
	 * 1. We assume that if unlock() is being called, it is because the current
	 *    thread is holding the lock, which means that the node to which "head"
	 *    points to is the one previous to the node created by the current thread,
	 *    so now all that needs to be done is to advance the head to the next node
	 *    and free() the memory of the previous which is now inaccessible, and
	 *    its "next" field will never be de-referenced by any other thread.
	 */
	void unlock()
	{
		// We assume that if this function was called is because this thread is
		// currently holding the lock, which means that the head->next is mynode
		mpsc_mutex_node_t * prev = lock_.head.load(std::memory_order_relaxed);
		mpsc_mutex_node_t * mynode = prev->next.load(std::memory_order_relaxed);

		if (mynode == NULL) {
			// TODO: too many unlocks ???
			return;
		}
		lock_.head.store(mynode);
		free(prev);
	}
	
private:
	mpsc_mutex_t lock_;
};
/**********************************************************************/
/*
 * Uses the algorithm of the Ticket Lock described in section 2.2 by
 * John Mellor-Crummey and Michael Scott in 1991:
 * http://web.mit.edu/6.173/www/currentsemester/readings/R06-scalable-synchronization-1991.pdf
 *
 * Notice that the initial decision of whether to spin or enter the critical
 * section is reached in a wait-free way on x86 or other systems for which
 * atomic_fetch_add() is implemented with a single atomic instruction (XADD in
 * the x86 case).
 */
class TicketLock
{
	typedef struct
	{
		std::atomic<long> ingress;
		char padding[64];      // To avoid false sharing with the head and tail
		std::atomic<long> egress;
	} ticket_mutex_t;
	
public:

	TicketLock()
	{
		lock_.ingress.store(0);
		lock_.egress.store(0);
	}
	
	/*
	 * Locks the mutex
	 * Progress Condition: Blocking
	 *
	 * Notice that we don't need to do an acquire barrier the first time we read
	 * "egress" because there was an implicit acquire barrier in the
	 * atomic_fetch_add(ingress, 1) and between then and now, there is no
	 * possibility of another thread incrementing egress because they would have
	 * to increment ingress first, which would cause them to wait for the current
	 * thread.
	 */
	void lock()
	{
		long lingress = lock_.ingress.fetch_add(1);

		// If the ingress and egress match, then the lock as been acquired and
		// we don't even need to do an acquire-barrier.
		if (lingress == lock_.egress.load(std::memory_order_relaxed)) return;

		while (lingress != lock_.egress.load()) {
			std::this_thread::yield();
		}
		// This thread has acquired the lock on the mutex
	}


	/*
	 * Unlocks the mutex
	 * Progress Condition: Wait-Free Population Oblivious
	 *
	 * We could do a simple atomic_fetch_add(egress, 1) but it is faster to do
	 * the relaxed load followed by the store with release barrier.
	 * Notice that the load can be relaxed because the thread did an acquire
	 * barrier when it read the "ingress" with the atomic_fetch_add() back in
	 * ticket_mutex_lock() (or the acquire on reading "egress" at a second try),
	 * and we have the guarantee that "egress" has not changed since then.
	 */
	void unlock()
	{
		long legress = lock_.egress.load(std::memory_order_relaxed);
		lock_.egress.store(legress+1);
	}
	
private:
	ticket_mutex_t lock_;
};
/**********************************************************************/
