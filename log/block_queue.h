#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <queue>
#include "../locker.h"  // 假设原locker.h路径
using namespace std;
template <class T>
class block_queue {
public:
    block_queue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);  // 保持与原代码一致的处理方式
        }
        m_max_size = max_size;
    }

    ~block_queue() {
        clear();
    }

    void clear() {
        m_mutex.lock();
        while (!m_queue.empty()) {
            m_queue.pop();
        }
        m_mutex.unlock();
    }

    bool full() {
        m_mutex.lock();
        if (m_queue.size() >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty() {
        m_mutex.lock();
        bool ret = m_queue.empty();
        m_mutex.unlock();
        return ret;
    }

    bool front(T &value) {
        m_mutex.lock();
        if (m_queue.empty()) {
            m_mutex.unlock();
            return false;
        }
        value = m_queue.front();
        m_mutex.unlock();
        return true;
    }

    bool back(T &value) {
        m_mutex.lock();
        if (m_queue.empty()) {
            m_mutex.unlock();
            return false;
        }
        value = m_queue.back();
        m_mutex.unlock();
        return true;
    }

    int size() {
        m_mutex.lock();
        int ret = m_queue.size();
        m_mutex.unlock();
        return ret;
    }

    int max_size() {
        m_mutex.lock();
        int ret = m_max_size;
        m_mutex.unlock();
        return ret;
    }

    bool push(const T &item) {
        m_mutex.lock();
        if (m_queue.size() >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        m_queue.push(item);
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    bool pop(T &item) {
        m_mutex.lock();
        while (m_queue.empty()) {
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }
        item = m_queue.front();
        m_queue.pop();
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;
    std::queue<T> m_queue;
    int m_max_size;
};

#endif // BLOCK_QUEUE_H