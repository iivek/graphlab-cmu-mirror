#ifndef GRAPHLAB_SYNCHRONIZED_QUEUE_HPP
#define GRAPHLAB_SYNCHRONIZED_QUEUE_HPP


#include <queue>

#include <graphlab/parallel/pthread_tools.hpp>
namespace graphlab {
  /// \ingroup util_internal
  template <typename T>
  class synchronized_queue {

  public:
    synchronized_queue() { };
    ~synchronized_queue() { };

    void push(const T &item) {
      _queuelock.lock();
      _queue.push(item);
      _queuelock.unlock();
    }
  
    bool safepop(T * ret) {
      _queuelock.lock();
      if (_queue.size() == 0) {
        _queuelock.unlock();

        return false;
      }
      *ret = _queue.front();
      _queue.pop();
      _queuelock.unlock();
      return true;
    }
  
    T pop() {
      _queuelock.lock();
      T t = _queue.front();
      _queue.pop();
      _queuelock.unlock();
      return t;
    }
  
    size_t size() const{
      return _queue.size();
    }
  private:
    std::queue<T> _queue;
    spinlock _queuelock;
  };

}
#endif
