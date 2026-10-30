#pragma once

#include <atomic>
#include <memory>

// Hands immutable objects to the audio thread. The message side publishes; the audio side picks the
// newest one up at the start of a block and passes the one it let go of back for deletion.
template <typename T>
class HandOver
{
public:
    ~HandOver()
    {
        delete pending.exchange(nullptr);
        delete current;
        collect();
    }
    void publish(std::unique_ptr<T> item)
    {
        collect();
        delete pending.exchange(item.release());
    }
    void collect()
    {
        for (T* t = retired.exchange(nullptr); t != nullptr;)
        {
            T* next = t->nextRetired;
            delete t;
            t = next;
        }
    }
    const T* get() // audio thread
    {
        if (T* p = pending.exchange(nullptr))
        {
            if (current != nullptr)
            {
                current->nextRetired = retired.load();
                while (!retired.compare_exchange_weak(current->nextRetired, current)) {}
            }
            current = p;
        }
        return current;
    }

private:
    std::atomic<T*> pending { nullptr }, retired { nullptr };
    T* current = nullptr;
};
