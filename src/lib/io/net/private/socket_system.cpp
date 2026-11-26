#include "socket_system.hpp"

#include <bit>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <print>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "epoll.hpp"
#include "socket.hpp"
#include "types.hpp"

namespace sky::io {

[[nodiscard]] socket_system& socket_system::get() {
    static socket_system instance;
    return instance;
}

[[nodiscard]] exret_t
socket_system::get_free_socket_id(socket::id& out_id) const {
    assert(out_id == socket::id::INVALID);

    for (std::size_t word = 0; word < used_socket_bitmap_.get_size(); ++word) {
        const std::uint64_t free_bits = ~used_socket_bitmap_[word];
        if (free_bits == 0) {
            continue;
        }
        const std::size_t bit =
            static_cast<std::size_t>(std::countr_zero(free_bits));
        const std::size_t idx = word * BITMAP_WORD_BITS + bit;
        if (idx >= MAX_SOCKET_COUNT) {
            break;
        }

        // The fixed underlying type can represent these slot indices.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        out_id = static_cast<socket::id>(idx);
        return EXRET_SUCCESS;
    }

    return static_cast<exret_t>(socket_error::NO_FREE_SOCKET_SLOT);
}

[[nodiscard]] bool socket_system::is_use_socket_id(socket::id id) const {
    const std::int64_t value = static_cast<std::int64_t>(id);
    assert(value >= 0 && value < static_cast<std::int64_t>(MAX_SOCKET_COUNT));

    const std::size_t idx = static_cast<std::size_t>(value);
    const std::size_t word = idx / BITMAP_WORD_BITS;
    const std::size_t bit = idx % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;
    const bool is_used = (used_socket_bitmap_[word] & mask) != 0;

    assert(
        (is_used == true ? sockets_[idx].fd_ >= 0 : sockets_[idx].fd_ == -1) &&
        "socket slot bitmap and descriptor state must agree");
    assert(is_used == (sockets_[idx].state_ != socket::state::CLOSED) &&
           "socket slot bitmap and lifecycle state must agree");
    return is_used;
}

void socket_system::change_socket_used_state(socket::id id, bool is_use) {
    const std::int64_t value = static_cast<std::int64_t>(id);
    assert(value >= 0 && value < static_cast<std::int64_t>(MAX_SOCKET_COUNT));

    const std::size_t idx = static_cast<std::size_t>(value);
    const std::size_t word = idx / BITMAP_WORD_BITS;
    const std::size_t bit = idx % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;

    if (is_use == true) {
        used_socket_bitmap_[word] |= mask;
    } else {
        used_socket_bitmap_[word] &= ~mask;
    }
}

[[nodiscard]] exret_t
socket_system::create_socket(const socket::create_info& info,
                             socket::id& out_id) {
    assert(out_id == socket::id::INVALID);

    socket::id id = socket::id::INVALID;
    const exret_t result = get_free_socket_id(id);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "create_socket: no free socket slot "
                   "(max_socket_count={}, error_code={})\n",
                   MAX_SOCKET_COUNT, result);
        return static_cast<exret_t>(socket_error::CREATE_SOCKET_FAIL);
    }

    const int socket_type =
        static_cast<int>(info.type) | static_cast<int>(info.options);
    const int fd = ::socket(static_cast<int>(info.domain), socket_type,
                            static_cast<int>(info.protocol));
    if (fd == -1) {
        std::perror("socket");
        return static_cast<exret_t>(socket_error::CREATE_SOCKET_FAIL);
    }

    assert(is_use_socket_id(id) == false);
    socket& slot = sockets_[static_cast<std::int64_t>(id)];
    slot.fd_ = fd;
    slot.state_ = socket::state::CREATED;
    slot.domain_ = info.domain;
    slot.type_ = info.type;
    slot.protocol_ = info.protocol;
    slot.options_ = info.options;
    change_socket_used_state(id, true);
    out_id = id;

    return EXRET_SUCCESS;
}

void socket_system::destroy_socket(socket::id id) {
    assert(is_use_socket_id(id) == true);

    const std::int64_t socket_idx = static_cast<std::int64_t>(id);
    for (std::size_t epoll_idx = 0; epoll_idx < epolls_.get_size();
         ++epoll_idx) {
        const std::uint64_t mask = std::uint64_t{1} << epoll_idx;
        if ((used_epoll_bitmap_ & mask) == 0) {
            continue;
        }

        epoll& poller = epolls_[epoll_idx];
        if (poller.watch_events_[socket_idx] != epoll::event::NONE) {
            poller.unwatch(id);
        }
    }

    socket& slot = sockets_[socket_idx];
    const int result = ::close(slot.fd_);
    if (result == -1) {
        [[maybe_unused]] const int native_error = errno;
        std::perror("close");
        assert(native_error != EBADF &&
               "managed descriptor must remain open until system cleanup");
    }

    slot.reset();
    change_socket_used_state(id, false);
}

[[nodiscard]] socket& socket_system::get_socket(socket::id id) & {
    assert(is_use_socket_id(id) == true);
    return sockets_[static_cast<std::int64_t>(id)];
}

[[nodiscard]] exret_t
socket_system::get_free_epoll_id(epoll::id& out_id) const {
    assert(out_id == epoll::id::INVALID);

    const std::size_t index =
        static_cast<std::size_t>(std::countr_zero(~used_epoll_bitmap_));
    if (index >= MAX_EPOLL_COUNT) {
        return static_cast<exret_t>(epoll_error::NO_FREE_EPOLL_SLOT);
    }

    out_id = static_cast<epoll::id>(index);
    return EXRET_SUCCESS;
}

[[nodiscard]] bool socket_system::is_use_epoll_id(epoll::id id) const {
    const std::int64_t value = static_cast<std::int64_t>(id);
    assert(value >= 0 && value < static_cast<std::int64_t>(MAX_EPOLL_COUNT));

    const std::size_t index = static_cast<std::size_t>(value);
    const std::uint64_t mask = std::uint64_t{1} << index;
    const bool is_used = (used_epoll_bitmap_ & mask) != 0;

    assert((is_used == true ? epolls_[index].fd_ >= 0
                            : epolls_[index].fd_ == -1) &&
           "epoll slot bitmap and descriptor state must agree");
    return is_used;
}

void socket_system::change_epoll_used_state(epoll::id id, bool is_use) {
    const std::int64_t value = static_cast<std::int64_t>(id);
    assert(value >= 0 && value < static_cast<std::int64_t>(MAX_EPOLL_COUNT));

    const std::size_t idx = static_cast<std::size_t>(value);
    const std::uint64_t mask = std::uint64_t{1} << idx;
    if (is_use == true) {
        used_epoll_bitmap_ |= mask;
    } else {
        used_epoll_bitmap_ &= ~mask;
    }
}

[[nodiscard]] exret_t
socket_system::create_epoll(const epoll::create_info& info, epoll::id& out_id) {
    assert(out_id == epoll::id::INVALID);

    epoll::id id = epoll::id::INVALID;
    const exret_t result = get_free_epoll_id(id);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "create_epoll: no free epoll slot "
                   "(max_epoll_count={}, error_code={})\n",
                   MAX_EPOLL_COUNT, result);
        return static_cast<exret_t>(epoll_error::CREATE_ERROR);
    }

    const int epoll_fd = ::epoll_create1(static_cast<int>(info.flag));
    if (epoll_fd == -1) {
        std::perror("epoll_create1");
        return static_cast<exret_t>(epoll_error::CREATE_ERROR);
    }

    assert(is_use_epoll_id(id) == false);
    epoll& slot = epolls_[static_cast<std::int64_t>(id)];
    slot.fd_ = epoll_fd;
    change_epoll_used_state(id, true);
    out_id = id;
    return EXRET_SUCCESS;
}

void socket_system::destroy_epoll(epoll::id id) {
    assert(is_use_epoll_id(id) == true);

    epoll& slot = epolls_[static_cast<std::int64_t>(id)];
    if (::close(slot.fd_) == -1) {
        [[maybe_unused]] const int native_error = errno;
        std::perror("close");
        assert(native_error != EBADF &&
               "managed descriptor must remain open until system cleanup");
    }

    slot.reset();
    change_epoll_used_state(id, false);
}

[[nodiscard]] epoll& socket_system::get_epoll(epoll::id id) & {
    assert(is_use_epoll_id(id) == true);
    return epolls_[static_cast<std::int64_t>(id)];
}

} // namespace sky::io
