//
// Created by heshaoquan on 2025/9/3.
//

#ifndef VK_SDL2_VP_THREADSAFEQUEUE_H
#define VK_SDL2_VP_THREADSAFEQUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    explicit ThreadSafeQueue(size_t max_size): max_size_(max_size) {}

    void set_max_size(size_t max_size) {
        max_size_ = max_size;
    }

    size_t size() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool full() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size() == max_size_;
    }

    void push(T item);

    void pop(T& item);

    void front(T& item);

    void clear();

private:
    std::queue<T> queue_;
    size_t max_size_ = 30;

    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};


template<typename T>
void ThreadSafeQueue<T>::push(T item) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] {return queue_.size() < max_size_;});
    queue_.push(item);
    not_empty_.notify_one();
}

template<typename T>
void ThreadSafeQueue<T>::pop(T& item) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] {return !queue_.empty();});
    item = queue_.front();
    queue_.pop();
    not_full_.notify_one();
}

template<typename T>
void ThreadSafeQueue<T>::front(T &item) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] {return !queue_.empty();});
    item = queue_.front();
}

template<typename T>
void ThreadSafeQueue<T>::clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_ = std::queue<T>();
    not_full_.notify_all();
}

#endif //VK_SDL2_VP_THREADSAFEQUEUE_H
