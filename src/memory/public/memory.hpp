#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "array.hpp"
#include "types.hpp"

namespace sky::memory {

enum class memory_error : exret_t {
    RESERVE_HEAP_FAIL = 1,
    COMMIT_HEAP_FAIL = 2,
    VIRTUAL_MEMORY_CREATE_FAIL = 3,
    NO_FREE_BUFFER_SLOT = 4,
};

struct buffer final {
    std::byte* base = nullptr;
    std::size_t size = 0;
};

class virtual_memory_system final
{
public:
    virtual_memory_system() = default;
    ~virtual_memory_system() = default;

    virtual_memory_system(const virtual_memory_system&) = delete;
    virtual_memory_system& operator=(const virtual_memory_system&) = delete;
    virtual_memory_system(virtual_memory_system&&) = delete;
    virtual_memory_system& operator=(virtual_memory_system&&) = delete;

    struct virtual_memory_create_info final {
        std::size_t reserve_pages = 0;
        std::size_t commit_pages = 0;
    };

    [[nodiscard]] exret_t
    create_heap_buffer(const virtual_memory_create_info& info,
                       std::int64_t& out_id);
    [[nodiscard]] exret_t resize_heap_buffer(std::int64_t id,
                                             std::size_t new_pages);
    void destroy_heap_buffer(std::int64_t id);
    [[nodiscard]] const buffer& get_heap_buffer(std::int64_t id) const {
        assert(is_use_id(id));
        return heap_slots_[id].buf;
    }

    constexpr std::size_t get_page_size() const {
        return HEAP_PAGE_SIZE;
    }

private:
    struct heap_slot final {
        buffer buf{};
        std::size_t reserved_pages = 0;
        std::size_t committed_pages = 0;
    };

    [[nodiscard]] exret_t reserve_heap(std::byte*& out, std::size_t pages);
    [[nodiscard]] exret_t commit_heap(std::byte* addr, std::size_t pages);
    void release_heap(std::byte*& base, std::size_t pages);
    [[nodiscard]] exret_t get_free_id(std::int64_t& out_id) const;
    void change_buffer_used_state(std::int64_t id, bool is_use);
    [[nodiscard]] bool is_use_id(std::int64_t id) const;

    static constexpr std::size_t HEAP_PAGE_SIZE = 4096;
    static constexpr std::size_t MAX_HEAP_COUNT = 1024;
    container::array<heap_slot, MAX_HEAP_COUNT> heap_slots_ = {};

    static constexpr std::size_t BITMAP_WORD_BITS = 64;
    static constexpr std::size_t BITMAP_WORD_COUNT =
        (MAX_HEAP_COUNT + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;
    container::array<std::uint64_t, BITMAP_WORD_COUNT> free_buffer_bitmap = {};
};

} // namespace sky::memory
