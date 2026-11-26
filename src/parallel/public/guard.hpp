#pragma once

#include <functional>
#include <utility>

#include "mutex.hpp"
#include "types.hpp"

namespace sky::parallel {

class guard final
{
public:
    explicit guard(mutex& target);
    ~guard() = default;

    guard(const guard&) = delete;
    guard& operator=(const guard&) = delete;
    guard(guard&&) = delete;
    guard& operator=(guard&&) = delete;

    template <typename Func, typename... Args>
    [[nodiscard]] exret_t protect(Func&& func, Args&&... args) const {
        const exret_t lock_result = mutex_.lock();
        if (lock_result != EXRET_SUCCESS) {
            return lock_result;
        }

        const exret_t func_result =
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        mutex_.unlock();
        return func_result;
    }

    template <typename Func, typename... Args>
    [[nodiscard]] exret_t protect_nowait(Func&& func, Args&&... args) const {
        const exret_t lock_result = mutex_.lock_nowait();
        if (lock_result != EXRET_SUCCESS) {
            return lock_result;
        }

        const exret_t func_result =
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        mutex_.unlock();
        return func_result;
    }

private:
    mutex& mutex_;
};

} // namespace sky::parallel
