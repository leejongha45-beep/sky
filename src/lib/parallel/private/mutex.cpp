#include "mutex.hpp"

#include <cassert>
#include <cerrno>
#include <cstdio> // IWYU pragma: keep
#include <cstring>
#include <print>
#include <pthread.h>

#include "types.hpp"

namespace sky::parallel {

exret_t mutex::create_attribute() {
    assert(attrs_.native_.has_value() == false &&
           "create_attribute requires empty native attribute storage");

    const int native_result =
        ::pthread_mutexattr_init(&attrs_.native_.emplace());
    if (native_result != 0) {
        attrs_.native_.reset();
        std::print(stderr, "pthread_mutexattr_init: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(mutex_error::INITIALIZE_MUTEX_FAIL);
    }
    return EXRET_SUCCESS;
}

void mutex::destroy_attribute() {
    assert(attrs_.native_.has_value() == true);

    const int native_result = ::pthread_mutexattr_destroy(&*attrs_.native_);
    attrs_.native_.reset();
    if (native_result != 0) {
        std::print(stderr, "pthread_mutexattr_destroy: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        assert(false &&
               "destroy_attribute requires initialized pthread attributes");
    }
}

exret_t mutex::initialize(const attributes::init_info& info) {
    const exret_t result = create_attribute();
    if (result != EXRET_SUCCESS) {
        return result;
    }

    const int type_result = ::pthread_mutexattr_settype(
        &*attrs_.native_, static_cast<int>(info.type));
    if (type_result != 0) {
        destroy_attribute();
        std::print(stderr, "pthread_mutexattr_settype: {} (error_code={})\n",
                   std::strerror(type_result), type_result);
        return static_cast<exret_t>(mutex_error::INITIALIZE_MUTEX_FAIL);
    }

    const int native_result = ::pthread_mutex_init(&native_, &*attrs_.native_);
    destroy_attribute();
    if (native_result != 0) {
        std::print(stderr, "pthread_mutex_init: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        return static_cast<exret_t>(mutex_error::INITIALIZE_MUTEX_FAIL);
    }

    attrs_.type_ = info.type;
    return EXRET_SUCCESS;
}

exret_t mutex::lock() {
    const int native_result = ::pthread_mutex_lock(&native_);
    if (native_result != 0) {
        std::print(stderr, "pthread_mutex_lock: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        assert(native_result == 0 && "lock requires an initialized mutex and "
                                     "must respect its recursion rules");
        return static_cast<exret_t>(mutex_error::LOCK_MUTEX_FAIL);
    }

    return EXRET_SUCCESS;
}

exret_t mutex::lock_nowait() {
    const int native_result = ::pthread_mutex_trylock(&native_);
    if (native_result == EBUSY) {
        return static_cast<exret_t>(mutex_error::LOCK_BUSY);
    }
    if (native_result != 0) {
        std::print(stderr, "pthread_mutex_trylock: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        assert(native_result == 0 && "lock_nowait requires an initialized "
                                     "mutex within its recursion limit");
        return static_cast<exret_t>(mutex_error::LOCK_MUTEX_FAIL);
    }

    return EXRET_SUCCESS;
}

void mutex::unlock() {
    const int native_result = ::pthread_mutex_unlock(&native_);
    if (native_result != 0) {
        std::print(stderr, "pthread_mutex_unlock: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        assert(
            false &&
            "unlock requires an initialized mutex owned by the calling thread");
    }
}

void mutex::destroy() {
    const int native_result = ::pthread_mutex_destroy(&native_);
    if (native_result != 0) {
        std::print(stderr, "pthread_mutex_destroy: {} (error_code={})\n",
                   std::strerror(native_result), native_result);
        assert(false && "destroy requires an initialized, unlocked mutex with "
                        "no users or waiters");
    }
}

} // namespace sky::parallel
