#include "task.h"

TaskQueue::TaskQueue()
{
}

void TaskQueue::put(const Task &v)
{
    lock.lock();

    while (tasks.size() >= DEFAULT_QUEUE_CAPACITY)
    {
        condition.wait(lock);
    }

    tasks.push(v);

    lock.unlock();

    condition.signal();
}

void TaskQueue::get(Task &v)
{
    lock.lock();

    while (tasks.size() == 0)
    {
        condition.wait(lock);
    }

    v = tasks.front();
    tasks.pop();

    lock.unlock();

    condition.signal();
}
