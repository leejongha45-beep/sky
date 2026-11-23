#pragma once

#include <cstddef>

#include "types.hpp"
#include "memory.hpp"

namespace sky::memory {
enum class alloc_error : exret_t {
    NOT_ENOUGH_SPACE = 100,
};

class bump_allocator final
{
public:
    bump_allocator() = default;
    ~bump_allocator() = default;

    bump_allocator(const bump_allocator&) = delete;
    bump_allocator& operator=(const bump_allocator&) = delete;
    bump_allocator(bump_allocator&&) = delete;
    bump_allocator& operator=(bump_allocator&&) = delete;

    struct init_info final {
        buffer memory = {};
        align_t align = ALIGN_ANY;
    };
    void initialize(const init_info& info);

    [[nodiscard]] exret_t allocate(std::size_t size, std::byte*& out_addr);
    void free(std::size_t size);

private:
    [[nodiscard]] std::size_t calc_padding(std::size_t size) const;

    buffer memory_ = {};
    std::size_t offset_ = 0;
    align_t align_ = ALIGN_ANY;
};

} // namespace sky::memory
