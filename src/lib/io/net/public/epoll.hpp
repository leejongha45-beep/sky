#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <sys/epoll.h> // IWYU pragma: keep

#include "array.hpp"
#include "socket.hpp"
#include "types.hpp"

namespace sky::io {

enum class epoll_error : exret_t {
    NO_FREE_EPOLL_SLOT = 400,
    CREATE_ERROR = 401,
    WATCH_ERROR = 402,
    UPDATE_WATCH_ERROR = 403,
    UNWATCH_ERROR = 404,
    WAIT_ERROR = 405,
    WAIT_INTERRUPTED = 406,
};

class epoll final
{
public:
    epoll() = default;
    ~epoll() = default;

    epoll(const epoll&) = delete;
    epoll& operator=(const epoll&) = delete;
    epoll(epoll&&) = delete;
    epoll& operator=(epoll&&) = delete;

    enum class id : std::int64_t {
        INVALID = INVALID_ID,
    };

    enum class flag : int {
        NONE = 0,
        CLOSE_ON_EXEC = EPOLL_CLOEXEC,
    };

    struct create_info final {
        epoll::flag flag = epoll::flag::NONE;
    };

    enum class event : std::uint32_t {
        NONE = 0,
        READABLE = EPOLLIN,
        WRITABLE = EPOLLOUT,
        PRIORITY = EPOLLPRI,
        ERROR = EPOLLERR,
        HANGUP = EPOLLHUP,
        PEER_WRITE_SHUTDOWN = EPOLLRDHUP,
        EDGE_TRIGGERED = EPOLLET,
        ONE_SHOT = EPOLLONESHOT,
        WAKEUP = EPOLLWAKEUP,
        EXCLUSIVE = EPOLLEXCLUSIVE,
    };

    [[nodiscard]] friend constexpr event operator|(event left, event right) {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        return static_cast<event>(static_cast<std::uint32_t>(left) |
                                  static_cast<std::uint32_t>(right));
    }

    struct ready_event final {
        socket::id socket_id = socket::id::INVALID;
        event events = event::NONE;
    };

    [[nodiscard]] static constexpr bool has_event(event events, event mask) {
        return (static_cast<std::uint32_t>(events) &
                static_cast<std::uint32_t>(mask)) != 0;
    }

    [[nodiscard]] exret_t watch(socket::id socket_id, event events);
    [[nodiscard]] exret_t update_watch(socket::id socket_id, event events);
    void unwatch(socket::id socket_id);
    [[nodiscard]] exret_t wait(std::span<ready_event> events, int timeout_ms,
                               std::size_t& out_count) const;

private:
    friend class socket_system;

    void reset();
    void reset_watch(std::size_t idx);

    static constexpr std::size_t MAX_WATCH_COUNT = 256;

    int fd_ = -1;
    container::array<event, MAX_WATCH_COUNT> watch_events_ = {};
};
} // namespace sky::io
