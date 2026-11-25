#pragma once

#include <cstddef>
#include <cstdint>

#include "array.hpp"
#include "thread.hpp"
#include "types.hpp"

namespace sky::parallel {

class thread_system final
{
public:
    [[nodiscard]] static thread_system& get();

    thread_system(const thread_system&) = delete;
    thread_system& operator=(const thread_system&) = delete;
    thread_system(thread_system&&) = delete;
    thread_system& operator=(thread_system&&) = delete;

    [[nodiscard]] exret_t create_thread(const thread::create_info& info,
                                        const thread::task& task,
                                        thread::id& out_id);
    void join_thread(thread::id id, void*& out_result);
    void detach_thread(thread::id id);

private:
    thread_system() = default;
    ~thread_system() = default;

    [[nodiscard]] exret_t get_free_thread_id(thread::id& out_id) const;
    [[nodiscard]] bool is_use_thread_id(thread::id id) const;
    void change_thread_used_state(thread::id id, bool is_use);

    static constexpr std::size_t MAX_THREAD_COUNT = 32;
    static_assert(MAX_THREAD_COUNT <= 64);

    container::array<thread, MAX_THREAD_COUNT> threads_ = {};
    std::uint64_t used_thread_bitmap_ = 0;
};

} // namespace sky::parallel
