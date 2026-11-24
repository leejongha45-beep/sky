#pragma once

#include <cstddef>
#include <cstdint>
#include <netinet/in.h> // IWYU pragma: keep
#include <span>
#include <sys/socket.h>

#include "types.hpp"

namespace sky::io {

enum class socket_error : exret_t {
    NO_FREE_SOCKET_SLOT = 300,
    CREATE_SOCKET_FAIL = 301,
    LISTEN_SOCKET_FAIL = 302,
    ACCEPT_SOCKET_FAIL = 303,
    RECV_SOCKET_FAIL = 304,
    SEND_SOCKET_FAIL = 305,
    CONNECT_SOCKET_FAIL = 306,
    CONNECT_IN_PROGRESS = 307,
    CONNECT_WOULD_BLOCK = 308,
    BIND_SOCKET_FAIL = 309,
    RECV_FROM_SOCKET_FAIL = 310,
    SEND_TO_SOCKET_FAIL = 311,
    ACCEPT_WOULD_BLOCK = 312,
    DATAGRAM_TRUNCATED = 313,
};

class socket final
{
public:
    socket() = default;
    ~socket() = default;

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;
    socket(socket&&) = delete;
    socket& operator=(socket&&) = delete;

    enum class domain : int {
        LOCAL = AF_UNIX,
        IPV4 = AF_INET,
        IPV6 = AF_INET6,
    };

    enum class type : int {
        STREAM = SOCK_STREAM,
        DATAGRAM = SOCK_DGRAM,
    };

    enum class option : int {
        NONE = 0,
        NON_BLOCKING = SOCK_NONBLOCK,
        CLOSE_ON_EXEC = SOCK_CLOEXEC,
    };

    [[nodiscard]] friend constexpr option operator|(option left, option right) {
        return static_cast<option>(static_cast<int>(left) |
                                   static_cast<int>(right));
    }

    enum class protocol : int {
        DEFAULT = 0,
        TCP = IPPROTO_TCP,
        UDP = IPPROTO_UDP,
    };

    struct create_info final {
        socket::domain domain;
        socket::type type;
        socket::protocol protocol;
        socket::option options;
    };

    struct address final {
        sockaddr_storage storage{};
        socklen_t size = 0;
    };

    [[nodiscard]] exret_t bind(const sockaddr* address, socklen_t address_size);
    [[nodiscard]] exret_t listen(int backlog);
    [[nodiscard]] exret_t accept(std::int64_t& out_id) const;
    [[nodiscard]] exret_t connect(const sockaddr* address,
                                  socklen_t address_size);
    [[nodiscard]] exret_t finish_connect();
    [[nodiscard]] exret_t recv(std::span<std::byte> dst,
                               std::size_t& out_bytes) const;
    [[nodiscard]] exret_t recv_from(std::span<std::byte> dst, address& sender,
                                    std::size_t& out_bytes) const;
    [[nodiscard]] exret_t send(std::span<const std::byte> src,
                               std::size_t& out_bytes) const;
    [[nodiscard]] exret_t send_to(std::span<const std::byte> src,
                                  const address& destination,
                                  std::size_t& out_bytes);
    [[nodiscard]] const address& get_local_address() const&;
    const address& get_local_address() const&& = delete;

private:
    friend class socket_system;

    enum class state {
        CLOSED,
        CREATED,
        BOUND,
        LISTENING,
        CONNECTING,
        CONNECTED,
        CONNECT_FAILED,
    };

    [[nodiscard]] bool address_matches_domain(const sockaddr* address,
                                              socklen_t address_size) const;
    void update_local_address();
    [[nodiscard]] bool has_local_port() const;

    int fd_ = -1;
    state state_ = state::CLOSED;
    domain domain_ = domain::LOCAL;
    type type_ = type::STREAM;
    protocol protocol_ = protocol::DEFAULT;
    option options_ = option::NONE;
    address local_address_{};
};

} // namespace sky::io
