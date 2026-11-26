#include "thread.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <pthread.h>
#include <sched.h>
#include <signal.h> // IWYU pragma: keep
#include <span>

// IWYU pragma: no_include <csignal>

#include "types.hpp"

namespace sky::parallel {

exret_t thread::create_attribute() {
    assert(attrs_.native_.has_value() == false &&
           "create_attribute requires empty native attribute storage");

    const int native_result = ::pthread_attr_init(&attrs_.native_.emplace());
    if (native_result != 0) {
        attrs_.native_.reset();
        std::print(stderr, "pthread_attr_init: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(
            thread_error::CREATE_THREAD_ATTRIBUTES_FAIL);
    }
    return EXRET_SUCCESS;
}

void thread::destroy_attribute() {
    assert(attrs_.native_.has_value() == true);

    const int native_result = ::pthread_attr_destroy(&*attrs_.native_);
    attrs_.native_.reset();
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_destroy: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        assert(false &&
               "destroy_attribute requires initialized pthread attributes");
    }
}

void thread::reset() {
    handle_ = {};
    attrs_.reset();
}

void thread::join(id thread_id, void*& out_result) {
    assert(attrs_.detach_ == attributes::detach_state::JOINABLE);
    assert(::pthread_equal(handle_, ::pthread_self()) == 0 &&
           "the join target must differ from the calling thread");

    void* result = nullptr;
    const int native_result = ::pthread_join(handle_, &result);

    reset();

    if (native_result == 0) {
        out_result = result;
    } else {
        std::print(stderr, "pthread_join: {} (id={}, error_code={})\n",
                   std::strerror(native_result),
                   static_cast<std::int64_t>(thread_id), native_result);
        assert(false && "join requires a valid joinable handle, one joiner, "
                        "and no join cycle");
    }
}

void thread::detach(id thread_id) {
    assert(attrs_.detach_ == attributes::detach_state::JOINABLE);

    const int native_result = ::pthread_detach(handle_);

    reset();

    if (native_result != 0) {
        std::print(stderr, "pthread_detach: {} (id={}, error_code={})\n",
                   std::strerror(native_result),
                   static_cast<std::int64_t>(thread_id), native_result);
        assert(false && "detach requires a valid joinable thread handle");
    }
}

void thread::attributes::reset() {
    assert(native_.has_value() == false &&
           "native thread attributes must be released before reset");

    stack_ = {};
    detach_ = detach_state::JOINABLE;
    scheduling_ = {};
    signal_source_ = signal_mask_source::INHERIT;
    cpu_mask_ = {};
    blocked_signal_mask_ = {};
}

exret_t thread::attributes::apply(const create_info& info) {
    assert(native_.has_value() == true &&
           "apply requires initialized pthread attributes");
    assert(info.scheduling.scope == contention_scope::SYSTEM);
    assert(info.detach == detach_state::JOINABLE ||
           info.detach == detach_state::DETACHED);

    const int native_result =
        ::pthread_attr_setdetachstate(&*native_, static_cast<int>(info.detach));
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setdetachstate: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }

    detach_ = info.detach;
    exret_t result = apply_stack_attributes(info.stack);
    if (result == EXRET_SUCCESS) {
        result = apply_scheduling_attributes(info.scheduling);
    }
    if (result == EXRET_SUCCESS) {
        result = apply_cpu_affinity(info.affinity);
    }
    if (result == EXRET_SUCCESS) {
        result = apply_signal_mask(info.signals);
    }
    return result;
}

exret_t thread::attributes::apply_stack_attributes(const stack& stack_config) {
    if (!stack_config.buffer.empty()) {
        const int native_result =
            ::pthread_attr_setstack(&*native_, stack_config.buffer.data(),
                                    stack_config.buffer.size_bytes());
        if (native_result != 0) {
            std::print(stderr, "pthread_attr_setstack: {} (error_code={})\n",
                       std::strerror(native_result), native_result);
            return static_cast<exret_t>(
                thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
        }
        stack_ = stack_config;
        return EXRET_SUCCESS;
    }

    if (stack_config.size != DEFAULT_STACK_SIZE) {
        const int native_result =
            ::pthread_attr_setstacksize(&*native_, stack_config.size);
        if (native_result != 0) {
            std::print(stderr,
                       "pthread_attr_setstacksize: {} (error_code={})\n",
                       std::strerror(native_result), native_result);
            return static_cast<exret_t>(
                thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
        }
    }
    if (stack_config.guard != DEFAULT_GUARD_SIZE) {
        const int native_result =
            ::pthread_attr_setguardsize(&*native_, stack_config.guard);
        if (native_result != 0) {
            std::print(stderr,
                       "pthread_attr_setguardsize: {} (error_code={})\n",
                       std::strerror(native_result), native_result);
            return static_cast<exret_t>(
                thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
        }
    }
    stack_ = stack_config;
    return EXRET_SUCCESS;
}

exret_t thread::attributes::apply_scheduling_attributes(
    const scheduling& scheduling_config) {
    int native_result =
        ::pthread_attr_setscope(&*native_, PTHREAD_SCOPE_SYSTEM);
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setscope: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }
    if (scheduling_config.source != scheduling_source::EXPLICIT) {
        scheduling_ = scheduling_config;
        return EXRET_SUCCESS;
    }

    int policy = SCHED_OTHER;
    switch (scheduling_config.policy) {
    case scheduling_policy::OTHER:
        break;
    case scheduling_policy::FIFO:
        policy = SCHED_FIFO;
        break;
    case scheduling_policy::ROUND_ROBIN:
        policy = SCHED_RR;
        break;
    }

    native_result = ::pthread_attr_setschedpolicy(&*native_, policy);
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setschedpolicy: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }

    // NOLINTNEXTLINE(misc-include-cleaner)
    sched_param parameters = {};
    parameters.sched_priority = scheduling_config.priority;
    native_result = ::pthread_attr_setschedparam(&*native_, &parameters);
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setschedparam: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }

    native_result =
        ::pthread_attr_setinheritsched(&*native_, PTHREAD_EXPLICIT_SCHED);
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setinheritsched: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }
    scheduling_ = scheduling_config;
    return EXRET_SUCCESS;
}

exret_t
thread::attributes::apply_cpu_affinity(std::span<const std::size_t> affinity) {
    if (affinity.empty()) {
        return EXRET_SUCCESS;
    }

    cpu_set_t cpu_mask;
    CPU_ZERO(&cpu_mask);
    for (const std::size_t cpu : affinity) {
        if (cpu >= static_cast<std::size_t>(CPU_SETSIZE)) {
            std::print(stderr,
                       "apply_cpu_affinity: CPU index {} exceeds supported "
                       "range (max={})\n",
                       cpu, CPU_SETSIZE - 1);
            return static_cast<exret_t>(
                thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
        }
        CPU_SET(cpu, &cpu_mask);
    }

    const int native_result =
        ::pthread_attr_setaffinity_np(&*native_, sizeof(cpu_mask), &cpu_mask);
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setaffinity_np: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }
    cpu_mask_ = cpu_mask;
    return EXRET_SUCCESS;
}

exret_t
thread::attributes::apply_signal_mask(const signal_mask& signal_mask_config) {
    if (signal_mask_config.source != signal_mask_source::EXPLICIT) {
        signal_source_ = signal_mask_config.source;
        return EXRET_SUCCESS;
    }

    // NOLINTNEXTLINE(misc-include-cleaner)
    sigset_t blocked_signal_mask = {};
    if (::sigemptyset(&blocked_signal_mask) == -1) {
        const int native_result = errno;
        std::print(stderr, "sigemptyset: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }
    for (const int signal : signal_mask_config.blocked) {
        if (::sigaddset(&blocked_signal_mask, signal) == -1) {
            const int native_result = errno;
            std::print(stderr, "sigaddset: {} (signal={}, error_code={})\n",
                       std::strerror(native_result), signal, native_result);
            return static_cast<exret_t>(
                thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
        }
    }
    const int native_result =
        ::pthread_attr_setsigmask_np(&*native_, &blocked_signal_mask);
    if (native_result != 0) {
        std::print(stderr, "pthread_attr_setsigmask_np: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(thread_error::APPLY_THREAD_ATTRIBUTES_FAIL);
    }
    blocked_signal_mask_ = blocked_signal_mask;
    signal_source_ = signal_mask_config.source;
    return EXRET_SUCCESS;
}

} // namespace sky::parallel
