#include "allocator.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "memory.hpp"
#include "types.hpp"

namespace sky::memory {
[[nodiscard]] std::size_t bump_allocator::calc_padding(std::size_t size) const {
    if (align_ == ALIGN_ANY) {
        return 0;
    }

    const std::size_t remainder = size % align_;
    const std::size_t padding = remainder == 0 ? 0 : align_ - remainder;
    return padding;
}

void bump_allocator::initialize(const init_info& info) {
    assert(memory_.base == nullptr);
    assert(info.memory.base != nullptr);
    assert(info.memory.size > 0);
    assert(info.align == ALIGN_ANY ||
           reinterpret_cast<std::uintptr_t>(info.memory.base) % info.align ==
               0);

    memory_ = info.memory;
    offset_ = 0;
    align_ = info.align;
}

[[nodiscard]] exret_t
bump_allocator::allocate(std::size_t size, std::byte*& out_addr) {
    assert(out_addr == nullptr);
    assert(size > 0);
    assert(memory_.base != nullptr);

    const std::size_t remaining = memory_.size - offset_;
    const std::size_t padding = calc_padding(size);
    if (size > remaining || padding > remaining - size) {
        return static_cast<exret_t>(alloc_error::NOT_ENOUGH_SPACE);
    }
    out_addr = memory_.base + offset_;
    offset_ += size + padding;

    return EXRET_SUCCESS;
}

void bump_allocator::free(std::size_t size) {
    assert(size > 0);
    assert(memory_.base != nullptr);

    const std::size_t padding = calc_padding(size);
    assert(size <= offset_ && padding <= offset_ - size);
    offset_ -= size + padding;
}
} // namespace sky::memory
