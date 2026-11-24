#include "socket.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <print>
#include <span>
#include <sys/socket.h>
#include <sys/types.h>

#include "array.hpp"
#include "socket_system.hpp"
#include "types.hpp"

namespace sky::io {

[[nodiscard]] bool
socket::address_matches_domain(const sockaddr* address,
                               socklen_t address_size) const {
    decltype(sockaddr::sa_family) family = 0;
    if (address == nullptr || address_size < sizeof(family)) {
        return false;
    }

    std::memcpy(&family, address, sizeof(family));
    return family == static_cast<decltype(family)>(domain_);
}

[[nodiscard]] exret_t socket::bind(const sockaddr* address,
                                   socklen_t address_size) {
    assert(fd_ >= 0);
    assert(state_ == state::CREATED);
    assert(address_matches_domain(address, address_size));

    if (::bind(fd_, address, address_size) == -1) {
        std::perror("bind");
        return static_cast<exret_t>(socket_error::BIND_SOCKET_FAIL);
    }

    state_ = state::BOUND;
    update_local_address();
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::listen(int backlog) {
    assert(fd_ >= 0);
    assert(type_ == type::STREAM);
    assert(state_ == state::BOUND || state_ == state::LISTENING ||
           (state_ == state::CREATED && domain_ != domain::LOCAL));
    assert(backlog >= 0);

    if (::listen(fd_, backlog) == -1) {
        std::perror("listen");
        return static_cast<exret_t>(socket_error::LISTEN_SOCKET_FAIL);
    }

    state_ = state::LISTENING;
    update_local_address();
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::accept(std::int64_t& out_id) const {
    assert(fd_ >= 0);
    assert(state_ == state::LISTENING);
    assert(out_id == INVALID_ID);

    socket_system& system = socket_system::get();
    std::int64_t id = INVALID_ID;
    const exret_t result = system.get_free_id(id);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "accept: no free socket slot "
                   "(max_socket_count={}, error_code={})\n",
                   socket_system::MAX_SOCKET_COUNT, result);
        return result;
    }

    address peer{};
    peer.size = sizeof(peer.storage);
    const int flags = static_cast<int>(options_);
    const int accepted_fd = ::accept4(
        fd_, reinterpret_cast<sockaddr*>(&peer.storage), &peer.size, flags);
    if (accepted_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return static_cast<exret_t>(socket_error::ACCEPT_WOULD_BLOCK);
        }
        std::perror("accept4");
        return static_cast<exret_t>(socket_error::ACCEPT_SOCKET_FAIL);
    }

    assert(system.is_use_id(id) == false);
    socket& slot = system.sockets_[id];
    slot.fd_ = accepted_fd;
    slot.state_ = state::CONNECTED;
    slot.domain_ = domain_;
    slot.type_ = type_;
    slot.protocol_ = protocol_;
    slot.options_ = options_;
    slot.update_local_address();
    slot.store_peer_address(peer);
    system.change_socket_used_state(id, true);
    out_id = id;
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::connect(const sockaddr* address,
                                      socklen_t address_size) {
    assert(fd_ >= 0);
    assert(state_ == state::CREATED || state_ == state::BOUND ||
           (state_ == state::CONNECTED && type_ == type::DATAGRAM));
    assert(address_matches_domain(address, address_size));

    if (::connect(fd_, address, address_size) == 0) {
        state_ = state::CONNECTED;
        update_local_address();
        update_peer_address();
        return EXRET_SUCCESS;
    }

    if (errno == EINPROGRESS) {
        state_ = state::CONNECTING;
        peer_address_ = {};
        return static_cast<exret_t>(socket_error::CONNECT_IN_PROGRESS);
    }
    if (errno == EAGAIN && domain_ == domain::LOCAL) {
        return static_cast<exret_t>(socket_error::CONNECT_WOULD_BLOCK);
    }

    std::perror("connect");
    state_ = state::CONNECT_FAILED;
    peer_address_ = {};
    return static_cast<exret_t>(socket_error::CONNECT_SOCKET_FAIL);
}

[[nodiscard]] exret_t socket::finish_connect() {
    assert(fd_ >= 0);
    assert(state_ == state::CONNECTING);

    pollfd pending{
        .fd = fd_,
        .events = POLLOUT,
        .revents = 0,
    };
    const int ready = ::poll(&pending, 1, 0);
    if (ready == 0 || (ready == -1 && errno == EINTR)) {
        return static_cast<exret_t>(socket_error::CONNECT_IN_PROGRESS);
    }
    if (ready == -1) {
        std::perror("poll");
        state_ = state::CONNECT_FAILED;
        return static_cast<exret_t>(socket_error::CONNECT_SOCKET_FAIL);
    }

    int connect_error = 0;
    socklen_t size = sizeof(connect_error);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &connect_error, &size) == -1) {
        std::perror("getsockopt");
        state_ = state::CONNECT_FAILED;
        return static_cast<exret_t>(socket_error::CONNECT_SOCKET_FAIL);
    }
    if (connect_error != 0) {
        errno = connect_error;
        std::perror("connect");
        state_ = state::CONNECT_FAILED;
        return static_cast<exret_t>(socket_error::CONNECT_SOCKET_FAIL);
    }

    state_ = state::CONNECTED;
    update_local_address();
    update_peer_address();
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::recv(std::span<std::byte> dst,
                                   std::size_t& out_bytes) const {
    assert(fd_ >= 0);
    assert(state_ == state::CONNECTED ||
           (type_ == type::DATAGRAM &&
            (state_ == state::CREATED || state_ == state::BOUND)));
    assert(dst.data() != nullptr || dst.empty());
    assert(type_ != type::STREAM || !dst.empty());
    assert(out_bytes == 0);

    const int flags = type_ == type::DATAGRAM ? MSG_TRUNC : 0;
    const ssize_t result = ::recv(fd_, dst.data(), dst.size(), flags);
    if (result == -1) {
        std::perror("recv");
        return static_cast<exret_t>(socket_error::RECV_SOCKET_FAIL);
    }

    const std::size_t received = static_cast<std::size_t>(result);
    if (received > dst.size()) {
        out_bytes = dst.size();
        return static_cast<exret_t>(socket_error::DATAGRAM_TRUNCATED);
    }

    out_bytes = received;
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::recv_from(std::span<std::byte> dst,
                                        address& sender,
                                        std::size_t& out_bytes) const {
    assert(fd_ >= 0);
    assert(type_ == type::DATAGRAM);
    assert(state_ == state::CREATED || state_ == state::BOUND ||
           state_ == state::CONNECTED);
    assert(dst.data() != nullptr || dst.empty());
    assert(sender.size == 0);
    assert(out_bytes == 0);

    address source{};
    source.size = sizeof(source.storage);
    const ssize_t result =
        ::recvfrom(fd_, dst.data(), dst.size(), MSG_TRUNC,
                   reinterpret_cast<sockaddr*>(&source.storage), &source.size);
    if (result == -1) {
        std::perror("recvfrom");
        return static_cast<exret_t>(socket_error::RECV_FROM_SOCKET_FAIL);
    }

    sender = source;
    const std::size_t received = static_cast<std::size_t>(result);
    if (received > dst.size()) {
        out_bytes = dst.size();
        return static_cast<exret_t>(socket_error::DATAGRAM_TRUNCATED);
    }

    out_bytes = received;
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::send(std::span<const std::byte> src,
                                   std::size_t& out_bytes) const {
    assert(fd_ >= 0);
    assert(state_ == state::CONNECTED);
    assert(src.data() != nullptr || src.empty());
    assert(out_bytes == 0);

    const ssize_t result = ::send(fd_, src.data(), src.size(), MSG_NOSIGNAL);
    if (result == -1) {
        std::perror("send");
        return static_cast<exret_t>(socket_error::SEND_SOCKET_FAIL);
    }

    out_bytes = static_cast<std::size_t>(result);
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t socket::send_to(std::span<const std::byte> src,
                                      const address& destination,
                                      std::size_t& out_bytes) {
    assert(fd_ >= 0);
    assert(type_ == type::DATAGRAM);
    assert(state_ == state::CREATED || state_ == state::BOUND ||
           state_ == state::CONNECTED);
    assert(src.data() != nullptr || src.empty());
    assert(out_bytes == 0);
    assert(destination.size <= sizeof(destination.storage));
    const sockaddr* const target =
        reinterpret_cast<const sockaddr*>(&destination.storage);
    assert(address_matches_domain(target, destination.size));

    const ssize_t result = ::sendto(fd_, src.data(), src.size(), MSG_NOSIGNAL,
                                    target, destination.size);
    if (result == -1) {
        std::perror("sendto");
    }

    if (state_ == state::CREATED && domain_ != domain::LOCAL) {
        update_local_address();
        if (result >= 0 || has_local_port()) {
            state_ = state::BOUND;
        } else {
            local_address_ = {};
        }
    }
    if (result == -1) {
        return static_cast<exret_t>(socket_error::SEND_TO_SOCKET_FAIL);
    }

    out_bytes = static_cast<std::size_t>(result);
    return EXRET_SUCCESS;
}

[[nodiscard]] const socket::address& socket::get_local_address() const& {
    assert(fd_ >= 0);
    return local_address_;
}

[[nodiscard]] bool socket::has_peer_address() const {
    return peer_address_.size > 0;
}

[[nodiscard]] const socket::address& socket::get_peer_address() const& {
    assert(has_peer_address());
    return peer_address_;
}

void socket::update_local_address() {
    address local{};
    local.size = sizeof(local.storage);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&local.storage),
                      &local.size) == -1) {
        std::perror("getsockname");
        local_address_ = {};
        return;
    }

    local_address_ = local;
}

void socket::update_peer_address() {
    address peer{};
    peer.size = sizeof(peer.storage);
    if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&peer.storage),
                      &peer.size) == -1) {
        std::perror("getpeername");
        peer_address_ = {};
        return;
    }

    store_peer_address(peer);
}

void socket::store_peer_address(const address& peer) {
    if (peer.size <= sizeof(peer.storage.ss_family)) {
        peer_address_ = {};
        return;
    }

    peer_address_ = peer;
}

[[nodiscard]] bool socket::has_local_port() const {
    assert(domain_ == domain::IPV4 || domain_ == domain::IPV6);

    const std::size_t offset = domain_ == domain::IPV4
                                   ? offsetof(sockaddr_in, sin_port)
                                   : offsetof(sockaddr_in6, sin6_port);
    in_port_t port = 0;
    if (local_address_.size < offset + sizeof(port)) {
        return false;
    }

    const std::byte* const data =
        reinterpret_cast<const std::byte*>(&local_address_.storage);
    std::memcpy(&port, data + offset, sizeof(port));
    return port != 0;
}

} // namespace sky::io
