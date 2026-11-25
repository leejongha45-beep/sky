#include "thread_system.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <pthread.h>

#include "thread.hpp"
#include "types.hpp"

namespace sky::parallel {

[[nodiscard]] thread_system& thread_system::get() {
    static thread_system instance;
    return instance;
}

[[nodiscard]] exret_t
thread_system::get_free_thread_id(thread::id& out_id) const {
    assert(out_id == thread::id::INVALID);

    const std::size_t idx =
        static_cast<std::size_t>(std::countr_zero(~used_thread_bitmap_));
    if (idx >= MAX_THREAD_COUNT) {
        return static_cast<exret_t>(thread_error::NO_FREE_THREAD_SLOT);
    }

    out_id = static_cast<thread::id>(idx);
    return EXRET_SUCCESS;
}

[[nodiscard]] bool thread_system::is_use_thread_id(thread::id id) const {
    const std::int64_t idx = static_cast<std::int64_t>(id);
    assert(idx >= 0 && idx < static_cast<std::int64_t>(MAX_THREAD_COUNT));

    const std::uint64_t mask = std::uint64_t{1} << idx;
    return (used_thread_bitmap_ & mask) != 0;
}

void thread_system::change_thread_used_state(thread::id id, bool is_use) {
    const std::int64_t idx = static_cast<std::int64_t>(id);
    assert(idx >= 0 && idx < static_cast<std::int64_t>(MAX_THREAD_COUNT));

    const std::uint64_t mask = std::uint64_t{1} << idx;
    if (is_use == true) {
        used_thread_bitmap_ |= mask;
    } else {
        used_thread_bitmap_ &= ~mask;
    }
}

[[nodiscard]] exret_t
thread_system::create_thread(const thread::create_info& info,
                             const thread::task& task, thread::id& out_id) {
    assert(out_id == thread::id::INVALID);
    assert(task.run != nullptr);

    thread::id id = thread::id::INVALID;
    exret_t result = get_free_thread_id(id);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "create_thread: no free thread slot "
                   "(max_thread_count={}, error_code={})\n",
                   MAX_THREAD_COUNT, result);
        return static_cast<exret_t>(thread_error::CREATE_THREAD_FAIL);
    }

    assert(is_use_thread_id(id) == false);
    thread& slot = threads_[static_cast<std::int64_t>(id)];
    result = slot.create_attribute();
    if (result != EXRET_SUCCESS) {
        slot.reset();
        return static_cast<exret_t>(thread_error::CREATE_THREAD_FAIL);
    }

    result = slot.attrs_.apply(info);
    if (result == EXRET_SUCCESS) {
        const int native_result = ::pthread_create(
            &slot.handle_, &slot.attrs_.native_, task.run, task.arg);
        if (native_result != 0) {
            std::print(stderr, "pthread_create: {} (error_code={})\n",
                       std::strerror(native_result), native_result);
            result = static_cast<exret_t>(thread_error::CREATE_THREAD_FAIL);
        }
    }

    slot.destroy_attribute();
    if (result != EXRET_SUCCESS) {
        slot.reset();
        return static_cast<exret_t>(thread_error::CREATE_THREAD_FAIL);
    }

    if (slot.attrs_.detach_ == thread::attributes::detach_state::DETACHED) {
        slot.reset();
        return EXRET_SUCCESS;
    }

    change_thread_used_state(id, true);
    out_id = id;
    return EXRET_SUCCESS;
}

void thread_system::join_thread(thread::id id, void*& out_result) {
    assert(id != thread::id::INVALID);
    assert(is_use_thread_id(id) == true);

    thread& slot = threads_[static_cast<std::int64_t>(id)];
    slot.join(id, out_result);
    change_thread_used_state(id, false);
}

void thread_system::detach_thread(thread::id id) {
    assert(id != thread::id::INVALID);
    assert(is_use_thread_id(id) == true);

    thread& slot = threads_[static_cast<std::int64_t>(id)];
    slot.detach(id);
    change_thread_used_state(id, false);
}

} // namespace sky::parallel
