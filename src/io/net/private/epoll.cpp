#include "epoll.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <sys/epoll.h>

#include "socket.hpp"
#include "socket_system.hpp"
#include "types.hpp"

namespace sky::io {

void epoll::reset() {
    fd_ = -1;
    for (std::size_t idx = 0; idx < watch_events_.get_size(); ++idx) {
        reset_watch(idx);
    }
}

void epoll::reset_watch(std::size_t idx) {
    watch_events_[idx] = event::NONE;
}

[[nodiscard]] exret_t epoll::watch(socket::id socket_id, event events) {
    assert(fd_ >= 0);
    assert(socket_id != socket::id::INVALID);
    assert(events != event::NONE);
    const int target_fd = socket_system::get().get_socket(socket_id).get_fd();
    assert(target_fd != fd_);

    epoll_event native_event = {};
    native_event.events = static_cast<std::uint32_t>(events);
    native_event.data.u64 = static_cast<std::uint64_t>(socket_id);
    if (::epoll_ctl(fd_, EPOLL_CTL_ADD, target_fd, &native_event) == -1) {
        std::perror("epoll_ctl");
        return static_cast<exret_t>(epoll_error::WATCH_ERROR);
    }

    watch_events_[static_cast<std::int64_t>(socket_id)] = events;
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t epoll::update_watch(socket::id socket_id, event events) {
    assert(fd_ >= 0);
    assert(socket_id != socket::id::INVALID);
    assert(events != event::NONE);
    const int target_fd = socket_system::get().get_socket(socket_id).get_fd();
    assert(target_fd != fd_);

    epoll_event native_event = {};
    native_event.events = static_cast<std::uint32_t>(events);
    native_event.data.u64 = static_cast<std::uint64_t>(socket_id);
    if (::epoll_ctl(fd_, EPOLL_CTL_MOD, target_fd, &native_event) == -1) {
        std::perror("epoll_ctl");
        return static_cast<exret_t>(epoll_error::UPDATE_WATCH_ERROR);
    }

    watch_events_[static_cast<std::int64_t>(socket_id)] = events;
    return EXRET_SUCCESS;
}

void epoll::unwatch(socket::id socket_id) {
    assert(fd_ >= 0);
    assert(socket_id != socket::id::INVALID);
    const int target_fd = socket_system::get().get_socket(socket_id).get_fd();
    assert(target_fd != fd_);

    if (::epoll_ctl(fd_, EPOLL_CTL_DEL, target_fd, nullptr) == -1) {
        std::perror("epoll_ctl");
    }

    reset_watch(static_cast<std::size_t>(socket_id));
}

[[nodiscard]] exret_t epoll::wait(std::span<ready_event> events, int timeout_ms,
                                  std::size_t& out_count) const {
    assert(fd_ >= 0);
    assert(!events.empty());
    assert(events.size() <=
           static_cast<std::size_t>(std::numeric_limits<int>::max()) /
               sizeof(epoll_event));
    assert(timeout_ms >= -1);
    assert(out_count == 0);

    std::array<epoll_event, socket_system::MAX_SOCKET_COUNT> native_events;
    const int max_events =
        static_cast<int>(std::min(events.size(), native_events.size()));
    const int result =
        ::epoll_wait(fd_, native_events.data(), max_events, timeout_ms);
    if (result == -1) {
        if (errno == EINTR) {
            return static_cast<exret_t>(epoll_error::WAIT_INTERRUPTED);
        }
        std::perror("epoll_wait");
        return static_cast<exret_t>(epoll_error::WAIT_ERROR);
    }

    out_count = static_cast<std::size_t>(result);
    for (std::size_t idx = 0; idx < out_count; ++idx) {
        const epoll_event& native_event = native_events[idx];
        events[idx] = {
            .socket_id = static_cast<socket::id>(native_event.data.u64),
            .events = static_cast<event>(native_event.events),
        };
    }
    return EXRET_SUCCESS;
}

} // namespace sky::io
