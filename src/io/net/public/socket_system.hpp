#pragma once

#include <cstddef>
#include <cstdint>

#include "array.hpp"
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
                                        std::int64_t& out_id);
    void close_socket(std::int64_t id);
    [[nodiscard]] socket& get_socket(std::int64_t id) &;

private:
    friend class socket;

    socket_system() = default;
    ~socket_system() = default;

    [[nodiscard]] exret_t get_free_id(std::int64_t& out_id) const;
    [[nodiscard]] bool is_use_id(std::int64_t id) const;
    void change_socket_used_state(std::int64_t id, bool is_use);

    static constexpr std::size_t MAX_SOCKET_COUNT = 256;
    static constexpr std::size_t BITMAP_WORD_BITS = 64;
    static constexpr std::size_t BITMAP_WORD_COUNT =
        (MAX_SOCKET_COUNT + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;

    container::array<socket, MAX_SOCKET_COUNT> sockets_ = {};
    container::array<std::uint64_t, BITMAP_WORD_COUNT> used_socket_bitmap_ = {};
};

} // namespace sky::io
