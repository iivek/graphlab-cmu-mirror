#ifndef GRAPHLAB_PTHREAD_TOOLS_HPP
#define GRAPHLAB_PTHREAD_TOOLS_HPP


#include <cstdlib>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <vector>
#include <list>
#include <iostream>
#include <boost/function.hpp>
#include <graphlab/logger/assertions.hpp>
#include <graphlab/parallel/atomic.hpp>
#include <graphlab/macros_def.hpp>

 #undef _POSIX_SPIN_LOCKS
#define _POSIX_SPIN_LOCKS -1


#define __likely__(x)       __builtin_expect((x),1)
#define __unlikely__(x)     __builtin_expect((x),0)




/**
 * \file pthread_tools.hpp A collection of utilities for threading
 */
namespace graphlab {



  
   
  /**
   * \class runnable Base class for defining a threaded function call.
   * A pointer to an instance of this class is passed to
   * thread_group. When the thread starts the run() function will be
   * called.
   */
  class runnable {
  public:
    //! The function that is executed when the thread starts 
    virtual void run() = 0;
    virtual ~runnable() {};
  };

   
  
  
  /**
   * \class thread is a basic thread which is runnable but also has
   * the ability to be started atomically by invoking start. To use
   * this class simply extend it, implement the runnable method and 
   * and invoke the start method.
   */
  class thread : public runnable {
  public:

    /**
     * This class contains the data unique to each thread. All threads
     * are gauranteed to have an associated graphlab thread_specific
     * data.
     */  
    class tls_data {
    public:
      inline tls_data(size_t thread_id) : thread_id_(thread_id) { }
      inline size_t thread_id() { return thread_id_; }
    private:
      size_t thread_id_;
    }; // end of thread specific data



    /// Static helper routines
    // ===============================================================

    /**
     * Get the thread specific data associated with this thread
     */
    static tls_data& get_tls_data();
      
    /** Get the id of the calling thread.  This will typically be the
        index in the thread group. Between 0 to ncpus. */
    static inline size_t thread_id() { return get_tls_data().thread_id(); }
    

    
    /**
     * This static method joins the invoking thread with the other
     * thread object.  This thread will not return from the join
     * routine until the other thread complets it run.
     */
    static void join(thread& other);
    
    // Called just before thread exits. Can be used
    // to do special cleanup... (need for Java JNI)
    static void thread_destroy_callback();
    static void set_thread_destroy_callback(void (*callback)());

      
    /**
     * Return the number processing units (individual cores) on this
     * system
     */
    static size_t cpu_count();


  private:
    
    //! Little helper function used to launch runnable objects      
    static void* invoke(void *_args);   

    /** Struct containing arguments to the invoke method which
        actually runs the thread */
    struct invoke_args {
      runnable* m_runnable;
      size_t m_thread_id;
      invoke_args(runnable* r, size_t t) : 
        m_runnable(r), m_thread_id(t) {
        ASSERT_TRUE(m_runnable != NULL);
      }
    }; // end of struct


    
    
  public:
    
    /**
     * Creates a thread that either runs the passed in runnable object
     * or otherwise invokes itself.
     */
    thread(runnable* obj = NULL, size_t thread_id = 0) : 
      m_stack_size(0), 
      m_p_thread(0),
      m_thread_id(thread_id),
      m_runnable(obj),
      m_active(false) {
      // Calculate the stack size in in bytes;
      const int BYTES_PER_MB = 1048576; 
      const int DEFAULT_SIZE_IN_MB = 8;
      m_stack_size = DEFAULT_SIZE_IN_MB * BYTES_PER_MB;
    }

    /**
     * execute this function to spawn a new thread running the run
     * routine provided by runnable:
     */
    void start() {
      // fill in the thread attributes
      pthread_attr_t attr;
      int error = 0;
      error = pthread_attr_init(&attr);
      ASSERT_TRUE(!error);
      error = pthread_attr_setstacksize(&attr, m_stack_size);
      ASSERT_TRUE(!error);
      error = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      ASSERT_TRUE(!error);
      

      // If no runnable object was passed in then this thread will try
      // and run itself.
      if(m_runnable == NULL) m_runnable = this;

    
      // Launch the thread.  Effectively this creates a new thread
      // which calls the invoke function passing in a pointer to a
      // runnable object
      invoke_args* args = new invoke_args(m_runnable, m_thread_id);
      error = pthread_create(&m_p_thread, 
                             &attr, 
                             invoke,  
                             static_cast<void*>(args) );
      m_active = true;
      if(error) {
        std::cout << "Major error in thread_group.launch (pthread_create). Error: " << error << std::endl;
        exit(EXIT_FAILURE);
      }
      // destroy the attribute object
      error = pthread_attr_destroy(&attr);
      ASSERT_TRUE(!error);
    }

    /**
     * Same as start() except that you can specify a CPU on which to
     * run the thread.  This only currently supported in Linux and if
     * invoked on a non Linux based system this will be equivalent to
     * start().
     */
    void start(size_t cpu_id){
      // if this is not a linux based system simply invoke start and
      // return;
#ifndef __linux__
      start();
      return;
#else
      // At this point we can ASSERT_TRUE that this is a linux system
      if (cpu_id >= cpu_count() && cpu_count() > 0) {
        // definitely invalid cpu_id
        std::cout << "Invalid cpu id passed on thread_ground.launch()" 
                  << std::endl;
        std::cout << "CPU " << cpu_id + 1 << " requested, but only " 
                  << cpu_count() << " CPUs available" << std::endl;
        exit(EXIT_FAILURE);
      }
      
      // fill in the thread attributes
      pthread_attr_t attr;
      int error = 0;
      error = pthread_attr_init(&attr);
      ASSERT_TRUE(!error);
      error = pthread_attr_setstacksize(&attr, m_stack_size);
      ASSERT_TRUE(!error);
      error = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      ASSERT_TRUE(!error);
      
      // Set Processor Affinity masks (linux only)
      cpu_set_t cpu_set;
      CPU_ZERO(&cpu_set);
      CPU_SET(cpu_id % CPU_SETSIZE, &cpu_set);
      pthread_attr_setaffinity_np(&attr, sizeof(cpu_set), &cpu_set);

      // If no runnable object was passed in then this thread will try
      // and run itself.
      if(m_runnable == NULL) m_runnable = this;
      
      // Launch the thread
      invoke_args* args = new invoke_args(m_runnable, m_thread_id);
      error = pthread_create(&m_p_thread, 
                             &attr, 
                             invoke,
                             static_cast<void*>(args) );
      m_active = true;
      if(error) {
        std::cout << "Major error in thread_group.launch" << std::endl;
        std::cout << "pthread_create() returned error " << error << std::endl;
        exit(EXIT_FAILURE);
      }
      
      
      
      // destroy the attribute object
      error = pthread_attr_destroy(&attr);
      ASSERT_TRUE(!error);
#endif
    } // end of start(size_t cpu_id)


    /**
     * Join the calling thread with this thread.
     */
    void join() {
      if(this == NULL) {
        std::cout << "Failure on join()" << std::endl;
        exit(EXIT_FAILURE);
      }
      join(*this);
    }

    bool active() const { return m_active; }


    /**
     * Default run routine is abstract and should be implemented by a
     * child class.
     */
    virtual void run() {
      std::cout << "Function run() is not implemented." << std::endl;
      exit(EXIT_FAILURE);
    }

    
    virtual ~thread() {  }

    virtual pthread_t pthreadid() {
      return m_p_thread;
    }
  private:
    
    
    //! The size of the internal stack for this thread
    size_t m_stack_size;
    
    //! The internal pthread object
    pthread_t m_p_thread;
    
    //! the threads id
    size_t m_thread_id;

    /**
     * The object this thread runs.  This object must stay in scope
     * for the duration of this thread
     */
    runnable* m_runnable;

    //! whether or not the thread is active (not currently used)
    bool m_active;

    
  }; // End of class thread

  



  /**
   * \class thread_group Manages a collection of threads
   */
  class thread_group {
    std::list< thread > m_threads;
    size_t m_thread_counter;
  public:
    /** 
     * Initializes a thread group. 
     */
    thread_group() : m_thread_counter(0) { }

    /** 
     * Launch a single thread which calls r->run() No CPU affinity is
     * set so which core it runs on is up to the OS Scheduler
     */
    void launch(runnable* r) {
      if(r == NULL) {
        std::cout << "Launching a NULL pointer." << std::endl;
        exit(EXIT_FAILURE);
      }
      // Create a thread object
      thread local_thread(r, m_thread_counter++);
      local_thread.start();
      // keep a local copy of the thread
      m_threads.push_back(local_thread);
    } // end of launch

    /**
     * Launch a single thread which calls r->run() Also sets CPU
     *  Affinity
     */
    void launch(runnable* r, size_t cpu_id) {
      if(r == NULL) {
        std::cout << "Launching a NULL pointer." << std::endl;
        exit(EXIT_FAILURE);
      }
      // Create a thread object
      thread local_thread(r, m_thread_counter++);
      local_thread.start(cpu_id);
      // keep a local copy of the thread
      m_threads.push_back(local_thread);
    } // end of launch

    //! Waits for all threads to complete execution
    void join() {
      while(!m_threads.empty()) {
        m_threads.front().join(); // Join the first thread
        m_threads.pop_front(); // remove the first element
      }
    }
    void signalall(int sig) {
      foreach (thread& t, m_threads) {
        pthread_kill(t.pthreadid(), sig);
      }
    }
    //! Destructor. Waits for all threads to complete execution
    ~thread_group(){ join(); }

  }; // End of thread group


  /**
  This provides runnable for a simple function.
  This should not be used directly, due to its self-deleting nature.
  Use the launch_in_new_thread() functions
  */
  class simple_function_thread : public runnable {
   public:
    simple_function_thread(boost::function<void (void)> f):f(f) { };

    inline void run() {
      f();
      delete this;
    }
    
   private:
    boost::function<void (void)> f;  
  };
  

  inline thread launch_in_new_thread(boost::function<void (void)> f, 
                               size_t cpuid = size_t(-1)) {
    runnable* r = new simple_function_thread(f);
    thread thr(r);
    if (cpuid != size_t(-1)) thr.start(cpuid);
    else thr.start();
    
    return thr;
  }

 
  inline void launch_in_new_thread(thread_group &thrgroup,
                            boost::function<void (void)> f, 
                            size_t cpuid = size_t(-1)) {
    runnable* r = new simple_function_thread(f);
    if (cpuid != size_t(-1)) thrgroup.launch(r, cpuid);
    else thrgroup.launch(r);
  }
  /**
   * \class mutex 
   * 
   * Wrapper around pthread's mutex On single core systems mutex
   * should be used.  On multicore systems, spinlock should be used.
   */
  class mutex {
  private:
    // mutable not actually needed
    mutable pthread_mutex_t m_mut;
  public:
    mutex() {
      int error = pthread_mutex_init(&m_mut, NULL);
      ASSERT_TRUE(!error);
    }
    inline void lock() const {
      int error = pthread_mutex_lock( &m_mut  );
      if (error) std::cout << "mutex.lock() error: " << error << std::endl;
      ASSERT_TRUE(!error);
    }
    inline void unlock() const {
      int error = pthread_mutex_unlock( &m_mut );
      ASSERT_TRUE(!error);
    }
    inline bool try_lock() const {
      return pthread_mutex_trylock( &m_mut ) == 0;
    }
    ~mutex(){
      int error = pthread_mutex_destroy( &m_mut );
      ASSERT_TRUE(!error);
    }
    friend class conditional;
  }; // End of Mutex

#if _POSIX_SPIN_LOCKS >= 0
  // We should change this to use a test for posix_spin_locks eventually
  
  // #ifdef __linux__
  /**
   * \class spinlock
   * 
   * Wrapper around pthread's spinlock On single core systems mutex
   * should be used.  On multicore systems, spinlock should be used.
   * If pthread_spinlock is not available, the spinlock will be
   * typedefed to a mutex
   */
  class spinlock {
  private:
    // mutable not actually needed
    mutable pthread_spinlock_t m_spin;
  public:
    spinlock () {
      int error = pthread_spin_init(&m_spin, PTHREAD_PROCESS_PRIVATE);
      ASSERT_TRUE(!error);
    }
  
    inline void lock() const { 
      int error = pthread_spin_lock( &m_spin  );
      ASSERT_TRUE(!error);
    }
    inline void unlock() const {
      int error = pthread_spin_unlock( &m_spin );
      ASSERT_TRUE(!error);
    }
    inline bool try_lock() const {
      return pthread_spin_trylock( &m_spin ) == 0;
    }
    ~spinlock(){
      int error = pthread_spin_destroy( &m_spin );
      ASSERT_TRUE(!error);
    }
    friend class conditional;
  }; // End of spinlock
#define SPINLOCK_SUPPORTED 1
#else
  //! if spinlock not supported, it is typedef it to a mutex.
  typedef mutex spinlock;
#define SPINLOCK_SUPPORTED 0
#endif

  
  
  class simple_spinlock {
  private:
    // mutable not actually needed
    mutable volatile char spinner;
  public:
    simple_spinlock () {
      spinner = 0;
    }
  
    inline void lock() const { 
      while(spinner == 1 || __sync_lock_test_and_set(&spinner, 1));
    }
    inline void unlock() const {
      __sync_synchronize();
      spinner = 0;
    }
    inline bool try_lock() const {
      return (__sync_lock_test_and_set(&spinner, 1) == 0);
    }
    ~simple_spinlock(){
      ASSERT_TRUE(spinner == 0);
    }
  };
  

  /**
   * \class conditional
   * Wrapper around pthread's condition variable
   */
  class conditional {
  private:
    mutable pthread_cond_t  m_cond;
  public:
    conditional() {
      int error = pthread_cond_init(&m_cond, NULL);
      ASSERT_TRUE(!error);
    }
    inline void wait(const mutex& mut) const {
      int error = pthread_cond_wait(&m_cond, &mut.m_mut);
      ASSERT_TRUE(!error);
    }
    inline int timedwait(const mutex& mut, int sec) const {
      struct timespec timeout;
      struct timeval tv;
      struct timezone tz;
      gettimeofday(&tv, &tz);
      timeout.tv_nsec = 0;
      timeout.tv_sec = tv.tv_sec + sec;
      return pthread_cond_timedwait(&m_cond, &mut.m_mut, &timeout);
    }
    inline int timedwait_ns(const mutex& mut, int ns) const {
      struct timespec timeout;
      struct timeval tv;
      struct timezone tz;
      gettimeofday(&tv, &tz);
      timeout.tv_nsec = (tv.tv_usec * 1000 + ns) % 1000000000;
      timeout.tv_sec = tv.tv_sec + (tv.tv_usec * 1000 + ns >= 1000000000);
      return pthread_cond_timedwait(&m_cond, &mut.m_mut, &timeout);
    }

    inline void signal() const {
      int error = pthread_cond_signal(&m_cond);
      ASSERT_TRUE(!error);
    }
    inline void broadcast() const {
      int error = pthread_cond_broadcast(&m_cond);
      ASSERT_TRUE(!error);
    }
    ~conditional() {
      int error = pthread_cond_destroy(&m_cond);
      ASSERT_TRUE(!error);
    }
  }; // End conditional

  /**
   * \class semaphore
   * Wrapper around pthread's semaphore
   */
#ifdef __APPLE__
  class semaphore {
  private:
    conditional cond;
    mutex mut;
    mutable volatile size_t semvalue;
    mutable volatile size_t waitercount;
  public:
    semaphore() {
      semvalue = 0;
      waitercount = 0;
    }
    inline void post() const {
      mut.lock();
      if (waitercount > 0) {
        cond.signal();
      }
      semvalue++;
      mut.unlock();
    }
    inline void wait() const {
      mut.lock();
      waitercount++;
      while (semvalue == 0) {
        cond.wait(mut);
      }
      waitercount--;
      semvalue--;
      mut.unlock();
    }
    ~semaphore() {
      ASSERT_TRUE(waitercount == 0);
      ASSERT_TRUE(semvalue == 0);
    }
  }; // End semaphore
#else
  class semaphore {
  private:
    mutable sem_t  m_sem;
  public:
    semaphore() {
      int error = sem_init(&m_sem, 0,0);
      ASSERT_TRUE(!error);
    }
    inline void post() const {
      int error = sem_post(&m_sem);
      ASSERT_TRUE(!error);
    }
    inline void wait() const {
      int error = sem_wait(&m_sem);
      ASSERT_TRUE(!error);
    }
    ~semaphore() {
      int error = sem_destroy(&m_sem);
      ASSERT_TRUE(!error);
    }
  }; // End semaphore
#endif
  

#define atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define cmpxchg(P, O, N) __sync_val_compare_and_swap((P), (O), (N))
#define atomic_inc(P) __sync_add_and_fetch((P), 1)

  /**
   * \class spinrwlock
   * rwlock built around "spinning"
   * source adapted from http://locklessinc.com/articles/locks/
   * "Scalable Reader-Writer Synchronization for Shared-Memory Multiprocessors"
   * John Mellor-Crummey and Michael Scott
   */
  class spinrwlock {

    union rwticket {
      unsigned u;
      unsigned short us;
      __extension__ struct {
        unsigned char write;
        unsigned char read;
        unsigned char users;
      } s;
    };
    mutable bool writing;
    mutable volatile rwticket l;
  public:
    spinrwlock() {
      memset(const_cast<rwticket*>(&l), 0, sizeof(rwticket));
    }
    inline void writelock() const {
      unsigned me = atomic_xadd(&l.u, (1<<16));
      unsigned char val = me >> 16;
    
      while (val != l.s.write) sched_yield();
      writing = true;
    }

    inline void wrunlock() const{
      rwticket t = *const_cast<rwticket*>(&l);

      t.s.write++;
      t.s.read++;
    
      *(volatile unsigned short *) (&l) = t.us;
      writing = false;
      __asm("mfence");
    }

    inline void readlock() const {
      unsigned me = atomic_xadd(&l.u, (1<<16));
      unsigned char val = me >> 16;
    
      while (val != l.s.read) sched_yield();
      l.s.read++;
    }

    inline void rdunlock() const {
      atomic_inc(&l.s.write);
    }
  
    inline void unlock() const {
      if (!writing) rdunlock();
      else wrunlock();
    }
  };

#undef atomic_xadd
#undef cmpxchg
#undef atomic_inc


  /**
   * \class rwlock
   * Wrapper around pthread's rwlock
   */
  class rwlock {
  private:
    mutable pthread_rwlock_t m_rwlock;
  public:
    rwlock() {
      int error = pthread_rwlock_init(&m_rwlock, NULL);
      ASSERT_TRUE(!error);
    }
    ~rwlock() {
      int error = pthread_rwlock_destroy(&m_rwlock);
      ASSERT_TRUE(!error);
    }
    inline void readlock() const {
      pthread_rwlock_rdlock(&m_rwlock);
      //ASSERT_TRUE(!error);
    }
    inline void writelock() const {
      pthread_rwlock_wrlock(&m_rwlock);
      //ASSERT_TRUE(!error);
    }
    inline void unlock() const {
      pthread_rwlock_unlock(&m_rwlock);
      //ASSERT_TRUE(!error);
    }
    inline void rdunlock() const {
      unlock();
    }
    inline void wrunlock() const {
      unlock();
    }
  }; // End rwlock

  /**
   * \class barrier
   * Wrapper around pthread's barrier
   */
#ifdef __linux__
  /**
   * \class barrier
   * Wrapper around pthread's barrier
   */
  class barrier {
  private:
    mutable pthread_barrier_t m_barrier;
  public:
    barrier(size_t numthreads) { pthread_barrier_init(&m_barrier, NULL, numthreads); }
    ~barrier() { pthread_barrier_destroy(&m_barrier); }
    inline void wait() const { pthread_barrier_wait(&m_barrier); }
  };

#else
  /**
   * \class barrier
   * Wrapper around pthread's barrier
   */
  class barrier {
  private:
    mutex m;
    int needed;
    int called;
    conditional c;
    
    bool barrier_sense;
    bool barrier_release;
    // we need the following to protect against spurious wakeups
  
  public:
    
    barrier(size_t numthreads) {
      needed = numthreads;
      called = 0;
      barrier_sense = false;
      barrier_release = true;
    }
    
    ~barrier() {}
    
    
    inline void wait() {
      m.lock();
      // set waiting;
      called++;
      bool listening_on = barrier_sense;
      
      if (called == needed) {
        // if I have reached the required limit, wait up. Set waiting
        // to 0 to make sure everyone wakes up

        called = 0;
        barrier_release = barrier_sense;
        barrier_sense = !barrier_sense;
        // clear all waiting
        c.broadcast();
      }
      else {
        // while no one has broadcasted, sleep
        while(barrier_release != listening_on) c.wait(m);
      }
      m.unlock();
    }
  };
#endif



  inline void prefetch_range(void *addr, size_t len) {
    char *cp;
    char *end = (char*)(addr) + len;

    for (cp = (char*)(addr); cp < end; cp += 64) __builtin_prefetch(cp, 0); 
  }
  inline void prefetch_range_write(void *addr, size_t len) {
    char *cp;
    char *end = (char*)(addr) + len;

    for (cp = (char*)(addr); cp < end; cp += 64) __builtin_prefetch(cp, 1);
  }



}; // End Namespace
#include <graphlab/macros_undef.hpp>
#endif
