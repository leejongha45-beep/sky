#pragma once

#include <optional>
#include <pthread.h> // IWYU pragma: keep

#include "types.hpp"

namespace sky::parallel {

enum class mutex_error : exret_t {
    INITIALIZE_MUTEX_FAIL = 600,
};

class mutex final
{
public:
    mutex() = default;
    ~mutex() = default;

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    mutex(mutex&&) = delete;
    mutex& operator=(mutex&&) = delete;

    class attributes final
    {
    public:
        attributes() = default;
        ~attributes() = default;

        attributes(const attributes&) = delete;
        attributes& operator=(const attributes&) = delete;
        attributes(attributes&&) = delete;
        attributes& operator=(attributes&&) = delete;

        enum class type : int {
            DEFAULT = PTHREAD_MUTEX_DEFAULT,
            ERRORCHECK = PTHREAD_MUTEX_ERRORCHECK,
            RECURSIVE = PTHREAD_MUTEX_RECURSIVE,
        };

        struct init_info final {
            attributes::type type = attributes::type::DEFAULT;
        };

    private:
        friend class mutex;

        type type_ = type::DEFAULT;
        // NOLINTNEXTLINE(misc-include-cleaner)
        std::optional<pthread_mutexattr_t> native_ = {};
    };

    [[nodiscard]] exret_t initialize(const attributes::init_info& info);

private:
    [[nodiscard]] exret_t create_attribute();
    void destroy_attribute();

    // NOLINTNEXTLINE(misc-include-cleaner)
    pthread_mutex_t native_ = {};
    attributes attrs_ = {};
};

} // namespace sky::parallel
