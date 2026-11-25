#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <pthread.h> // IWYU pragma: keep
#include <sched.h>
#include <signal.h> // IWYU pragma: keep
#include <span>
#include <tuple>

// IWYU pragma: no_include <csignal>
// IWYU pragma: no_include <bits/types/__sigset_t.h>

#include "types.hpp"

namespace sky::parallel {

enum class thread_error : exret_t {
    NO_FREE_THREAD_SLOT = 500,
    CREATE_THREAD_FAIL = 501,
    CREATE_THREAD_ATTRIBUTES_FAIL = 502,
    APPLY_THREAD_ATTRIBUTES_FAIL = 503,
};

class thread final
{
public:
    thread() = default;
    ~thread() = default;

    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;
    thread(thread&&) = delete;
    thread& operator=(thread&&) = delete;

    enum class id : std::int64_t {
        INVALID = INVALID_ID,
    };

    struct create_info;

    class attributes final
    {
    public:
        attributes() = default;
        ~attributes() = default;

        attributes(const attributes&) = delete;
        attributes& operator=(const attributes&) = delete;
        attributes(attributes&&) = delete;
        attributes& operator=(attributes&&) = delete;

        static constexpr std::size_t DEFAULT_STACK_SIZE = 0;
        static constexpr std::size_t DEFAULT_GUARD_SIZE =
            std::numeric_limits<std::size_t>::max();

        struct stack final {
            std::size_t size = DEFAULT_STACK_SIZE;
            std::span<std::byte> buffer = {};
            std::size_t guard = DEFAULT_GUARD_SIZE;
        };

        enum class detach_state : int {
            JOINABLE = PTHREAD_CREATE_JOINABLE,
            DETACHED = PTHREAD_CREATE_DETACHED,
        };

        enum class scheduling_source {
            INHERIT,
            EXPLICIT,
        };

        enum class scheduling_policy {
            OTHER,
            FIFO,
            ROUND_ROBIN,
        };

        enum class contention_scope {
            SYSTEM,
        };

        struct scheduling final {
            scheduling_source source = scheduling_source::INHERIT;
            scheduling_policy policy = scheduling_policy::OTHER;
            int priority = 0;
            contention_scope scope = contention_scope::SYSTEM;
        };

        enum class signal_mask_source {
            INHERIT,
            EXPLICIT,
        };

        struct signal_mask final {
            signal_mask_source source = signal_mask_source::INHERIT;
            std::span<const int> blocked = {};
        };

    private:
        friend class thread;
        friend class thread_system;

        void reset();
        [[nodiscard]] exret_t apply(const create_info& requested);
        [[nodiscard]] exret_t apply_stack_attributes(const stack& requested);
        [[nodiscard]] exret_t
        apply_scheduling_attributes(const scheduling& requested);
        [[nodiscard]] exret_t
        apply_cpu_affinity(std::span<const std::size_t> affinity);
        [[nodiscard]] exret_t apply_signal_mask(const signal_mask& requested);

        stack stack_ = {};
        detach_state detach_ = detach_state::JOINABLE;
        scheduling scheduling_ = {};
        signal_mask_source signal_source_ = signal_mask_source::INHERIT;

        // NOLINTNEXTLINE(misc-include-cleaner)
        pthread_attr_t native_ = {};
        cpu_set_t cpu_mask_ = {};
        // NOLINTNEXTLINE(misc-include-cleaner)
        sigset_t blocked_signal_mask_ = {};
    };

    using runner = void* (*)(void*);

    template <typename... Types>
    using arguments = std::tuple<Types...>;

    struct create_info final {
        thread::attributes::stack stack = {};
        thread::attributes::detach_state detach =
            thread::attributes::detach_state::JOINABLE;
        thread::attributes::scheduling scheduling = {};
        std::span<const std::size_t> affinity = {};
        thread::attributes::signal_mask signals = {};
    };

    struct task final {
        thread::runner run = nullptr;
        void* arg = nullptr;
    };

private:
    friend class thread_system;

    [[nodiscard]] exret_t create_attribute();
    void destroy_attribute();
    void reset();
    void join(id thread_id, void*& out_result);
    void detach(id thread_id);

    // NOLINTNEXTLINE(misc-include-cleaner)
    pthread_t handle_ = {};
    attributes attrs_ = {};
};

} // namespace sky::parallel
