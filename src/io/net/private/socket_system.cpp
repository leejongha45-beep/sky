#include "socket_system.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <print>
#include <sys/socket.h>
#include <unistd.h>

#include "socket.hpp"
#include "types.hpp"

namespace sky::io {

[[nodiscard]] socket_system& socket_system::get() {
    static socket_system instance;
    return instance;
}

[[nodiscard]] exret_t socket_system::get_free_id(std::int64_t& out_id) const {
    assert(out_id == INVALID_ID);

    for (std::size_t word = 0; word < used_socket_bitmap_.get_size(); ++word) {
        const std::uint64_t free_bits = ~used_socket_bitmap_[word];
        if (free_bits == 0) {
            continue;
        }
        const std::size_t bit =
            static_cast<std::size_t>(std::countr_zero(free_bits));
        const std::size_t index = word * BITMAP_WORD_BITS + bit;
        if (index >= MAX_SOCKET_COUNT) {
            break;
        }

        out_id = static_cast<std::int64_t>(index);
        return EXRET_SUCCESS;
    }

    return static_cast<exret_t>(socket_error::NO_FREE_SOCKET_SLOT);
}

[[nodiscard]] bool socket_system::is_use_id(std::int64_t id) const {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_SOCKET_COUNT));

    const std::size_t index = static_cast<std::size_t>(id);
    const std::size_t word = index / BITMAP_WORD_BITS;
    const std::size_t bit = index % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;
    const bool is_used = (used_socket_bitmap_[word] & mask) != 0;

    assert(is_used == true ? sockets_[id].fd_ >= 0 : sockets_[id].fd_ == -1);
    assert(is_used == (sockets_[id].state_ != socket::state::CLOSED));
    return is_used;
}

void socket_system::change_socket_used_state(std::int64_t id, bool is_use) {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_SOCKET_COUNT));

    const std::size_t index = static_cast<std::size_t>(id);
    const std::size_t word = index / BITMAP_WORD_BITS;
    const std::size_t bit = index % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;

    if (is_use == true) {
        used_socket_bitmap_[word] |= mask;
    } else {
        used_socket_bitmap_[word] &= ~mask;
    }
}

[[nodiscard]] exret_t
socket_system::create_socket(const socket::create_info& info,
                             std::int64_t& out_id) {
    assert(out_id == INVALID_ID);

    std::int64_t id = INVALID_ID;
    const exret_t result = get_free_id(id);
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

    assert(is_use_id(id) == false);
    socket& slot = sockets_[id];
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

void socket_system::close_socket(std::int64_t id) {
    assert(is_use_id(id) == true);

    socket& slot = sockets_[id];
    const int result = ::close(slot.fd_);
    if (result == -1) {
        std::perror("close");
    }

    slot.fd_ = -1;
    slot.state_ = socket::state::CLOSED;
    slot.domain_ = socket::domain::LOCAL;
    slot.type_ = socket::type::STREAM;
    slot.protocol_ = socket::protocol::DEFAULT;
    slot.options_ = socket::option::NONE;
    slot.local_address_ = {};
    change_socket_used_state(id, false);
}

[[nodiscard]] socket& socket_system::get_socket(std::int64_t id) & {
    assert(is_use_id(id) == true);
    return sockets_[id];
}

} // namespace sky::io
