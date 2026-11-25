#include "mutex.hpp"

#include <cassert>
#include <cstdio> // IWYU pragma: keep
#include <cstring>
#include <print>
#include <pthread.h>

#include "types.hpp"

namespace sky::parallel {

exret_t mutex::create_attribute() {
    assert(attrs_.native_.has_value() == false);

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

} // namespace sky::parallel
