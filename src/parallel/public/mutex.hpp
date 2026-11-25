#pragma once

#include <pthread.h> // IWYU pragma: keep

namespace sky::parallel {

class mutex final
{
public:
    mutex() = default;
    ~mutex() = default;

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    mutex(mutex&&) = delete;
    mutex& operator=(mutex&&) = delete;

private:
    // NOLINTNEXTLINE(misc-include-cleaner)
    pthread_mutex_t native_ = {};
    bool is_grabbed_ = false;
};

} // namespace sky::parallel
