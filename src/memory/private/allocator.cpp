#include "allocator.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

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

bump_allocator::init_info::init_info(const std::span<std::byte>& bytes,
                                     align_t alignment)
    : memory(bytes), align(alignment) {
}

void bump_allocator::initialize(const init_info& info) {
    assert(memory_ == nullptr);
    const std::span<std::byte>& bytes = info.memory;
    assert(bytes.data() != nullptr);
    assert(bytes.size() > 0);
    assert(info.align == ALIGN_ANY ||
           reinterpret_cast<std::uintptr_t>(bytes.data()) % info.align == 0);

    memory_ = &bytes;
    offset_ = 0;
    align_ = info.align;
}

[[nodiscard]] exret_t bump_allocator::allocate(std::size_t size,
                                               std::byte*& out_addr) {
    assert(out_addr == nullptr);
    assert(size > 0);
    assert(memory_ != nullptr);
    assert(memory_->data() != nullptr);

    const std::size_t capacity = memory_->size();
    if (offset_ > capacity) {
        return static_cast<exret_t>(alloc_error::NOT_ENOUGH_SPACE);
    }
    const std::size_t remaining = capacity - offset_;
    const std::size_t padding = calc_padding(size);
    if (size > remaining || padding > remaining - size) {
        return static_cast<exret_t>(alloc_error::NOT_ENOUGH_SPACE);
    }
    out_addr = memory_->data() + offset_;
    offset_ += size + padding;

    return EXRET_SUCCESS;
}

void bump_allocator::free(std::size_t size) {
    assert(size > 0);
    assert(memory_ != nullptr);
    assert(memory_->data() != nullptr);

    const std::size_t padding = calc_padding(size);
    assert(size <= offset_ && padding <= offset_ - size);
    offset_ -= size + padding;
}
} // namespace sky::memory
