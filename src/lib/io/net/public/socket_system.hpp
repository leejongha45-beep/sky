#pragma once

#include <cstddef>
#include <cstdint>

#include "array.hpp"
#include "epoll.hpp"
#include "socket.hpp"
#include "types.hpp"

namespace sky::io {

class socket_system final
{
public:
    [[nodiscard]] static socket_system& get();

    socket_system(const socket_system&) = delete;
    socket_system& operator=(const socket_system&) = delete;
    socket_system(socket_system&&) = delete;
    socket_system& operator=(socket_system&&) = delete;

    [[nodiscard]] exret_t create_socket(const socket::create_info& info,
                                        socket::id& out_id);
    void destroy_socket(socket::id id);
    [[nodiscard]] socket& get_socket(socket::id id) &;

    [[nodiscard]] exret_t create_epoll(const epoll::create_info& info,
                                       epoll::id& out_id);
    void destroy_epoll(epoll::id id);
    [[nodiscard]] epoll& get_epoll(epoll::id id) &;

private:
    friend class socket;
    friend class epoll;

    socket_system() = default;
    ~socket_system() = default;

    [[nodiscard]] exret_t get_free_socket_id(socket::id& out_id) const;
    [[nodiscard]] bool is_use_socket_id(socket::id id) const;
    void change_socket_used_state(socket::id id, bool is_use);

    [[nodiscard]] exret_t get_free_epoll_id(epoll::id& out_id) const;
    [[nodiscard]] bool is_use_epoll_id(epoll::id id) const;
    void change_epoll_used_state(epoll::id id, bool is_use);

    static constexpr std::size_t MAX_SOCKET_COUNT = 256;
    static_assert(MAX_SOCKET_COUNT == epoll::MAX_WATCH_COUNT,
                  "epoll requires one watch slot per socket id");
    static constexpr std::size_t BITMAP_WORD_BITS = 64;
    static constexpr std::size_t BITMAP_WORD_COUNT =
        (MAX_SOCKET_COUNT + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;

    container::array<socket, MAX_SOCKET_COUNT> sockets_ = {};
    container::array<std::uint64_t, BITMAP_WORD_COUNT> used_socket_bitmap_ = {};

    static constexpr std::size_t MAX_EPOLL_COUNT = 16;
    static_assert(MAX_EPOLL_COUNT <= BITMAP_WORD_BITS,
                  "epoll slots must fit in one bitmap word");
    container::array<epoll, MAX_EPOLL_COUNT> epolls_ = {};
    std::uint64_t used_epoll_bitmap_ = 0;
};

} // namespace sky::io
