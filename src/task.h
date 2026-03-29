#ifndef TASK_H
#define TASK_H

#include <memory>
#include <queue>
#include "platform.h"

// ncnn
#include "mat.h"
#include "platform.h" // ncnn platform for Mutex/ConditionVariable

static constexpr int DEFAULT_QUEUE_CAPACITY = 8;
static constexpr int TASK_SENTINEL_ID = -233;

// RAII wrapper for malloc'd pixel data. Copyable (shared ownership).
// When all copies are destroyed, the underlying buffer is freed automatically.
class ManagedPixelBuffer
{
public:
    ManagedPixelBuffer() = default;

    // Take ownership of a malloc'd buffer (frees any previously owned buffer).
    void reset(unsigned char *data)
    {
        data_ = std::shared_ptr<unsigned char>(data, free);
    }

    // Release ownership without freeing.
    void release()
    {
        data_.reset();
    }

    unsigned char *get() const { return data_.get(); }

private:
    std::shared_ptr<unsigned char> data_;
};

class Task
{
public:
    int id;
    int webp;

    path_t inpath;
    path_t outpath;

    ncnn::Mat inimage;
    ncnn::Mat outimage;

    // Owns any malloc'd pixel buffer that outimage.data points to.
    // When this is set, outimage wraps owned_output's buffer (not ncnn-managed).
    ManagedPixelBuffer owned_output;
};

class TaskQueue
{
public:
    TaskQueue();

    void put(const Task &v);
    void get(Task &v);

private:
    ncnn::Mutex lock;
    ncnn::ConditionVariable condition;
    std::queue<Task> tasks;
};

#endif // TASK_H
