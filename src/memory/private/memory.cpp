#include "memory.hpp"

#include <bit>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <print>
#include <span>
#include <sys/mman.h>

#include "types.hpp"

namespace sky::memory {

[[nodiscard]] virtual_memory_system& virtual_memory_system::get() {
    static virtual_memory_system instance;
    return instance;
}

[[nodiscard]] exret_t virtual_memory_system::reserve_heap(std::byte*& out,
                                                          std::size_t pages) {
    assert(out == nullptr);
    assert(pages > 0);

    void* base = ::mmap(nullptr, HEAP_PAGE_SIZE * pages, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        std::perror("mmap");
        return static_cast<exret_t>(memory_error::RESERVE_HEAP_FAIL);
    }

    out = static_cast<std::byte*>(base);

    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t virtual_memory_system::commit_heap(std::byte* addr,
                                                         std::size_t pages) {
    assert(addr != nullptr);
    assert(pages > 0);

    const int result =
        ::mprotect(addr, HEAP_PAGE_SIZE * pages, PROT_READ | PROT_WRITE);
    if (result != 0) {
        std::perror("mprotect");
        return static_cast<exret_t>(memory_error::COMMIT_HEAP_FAIL);
    }

    return EXRET_SUCCESS;
}

void virtual_memory_system::release_heap(std::byte*& base, std::size_t pages) {
    assert(base != nullptr);
    assert(pages > 0);

    const int result = ::munmap(base, HEAP_PAGE_SIZE * pages);
    if (result != 0) {
        [[maybe_unused]] const int native_error = errno;
        std::perror("munmap");
        assert(native_error != EINVAL &&
               "munmap requires the recorded reservation base and page count");
    }
    base = nullptr;
}

[[nodiscard]] exret_t
virtual_memory_system::get_free_id(std::int64_t& out_id) const {
    assert(out_id == INVALID_ID);

    for (std::size_t word = 0; word < free_buffer_bitmap_.get_size(); ++word) {
        const std::uint64_t free_bits = ~free_buffer_bitmap_[word];
        if (free_bits != 0) {
            const std::size_t bit =
                static_cast<std::size_t>(std::countr_zero(free_bits));
            const std::size_t id = word * BITMAP_WORD_BITS + bit;
            if (id >= MAX_HEAP_COUNT) {
                break;
            }
            out_id = static_cast<std::int64_t>(id);
            return EXRET_SUCCESS;
        }
    }

    return static_cast<exret_t>(memory_error::NO_FREE_BUFFER_SLOT);
}

void virtual_memory_system::change_buffer_used_state(std::int64_t id,
                                                     bool is_use) {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_HEAP_COUNT));

    const std::size_t index = static_cast<std::size_t>(id);
    const std::size_t word = index / BITMAP_WORD_BITS;
    const std::size_t bit = index % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;

    if (is_use) {
        free_buffer_bitmap_[word] |= mask;
    } else {
        free_buffer_bitmap_[word] &= ~mask;
    }
}

[[nodiscard]] bool virtual_memory_system::is_use_id(std::int64_t id) const {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_HEAP_COUNT));

    const std::size_t index = static_cast<std::size_t>(id);
    const std::size_t word = index / BITMAP_WORD_BITS;
    const std::size_t bit = index % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;

    const bool is_used = (free_buffer_bitmap_[word] & mask) != 0;

    assert(is_used == (heap_slots_[id].bytes.data() != nullptr) &&
           "heap slot bitmap and backing span address must agree");

    return is_used;
}

[[nodiscard]] exret_t virtual_memory_system::create_heap_buffer(
    const virtual_memory_create_info& info, std::int64_t& out_id) {
    assert(out_id == INVALID_ID);
    assert(info.commit_pages > 0);
    assert(info.reserve_pages >= info.commit_pages &&
           "initial commit must fit within the reservation");

    exret_t result = EXRET_SUCCESS;

    std::byte* base = nullptr;
    result = reserve_heap(base, info.reserve_pages);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "create_heap_buffer: reserve_heap failed "
                   "(reserve_pages={}, error_code={})\n",
                   info.reserve_pages, result);
        return static_cast<exret_t>(memory_error::VIRTUAL_MEMORY_CREATE_FAIL);
    }

    result = commit_heap(base, info.commit_pages);
    if (result != EXRET_SUCCESS) {
        release_heap(base, info.reserve_pages);
        std::print(stderr,
                   "create_heap_buffer: commit_heap failed "
                   "(commit_pages={}, reserve_pages={}, error_code={})\n",
                   info.commit_pages, info.reserve_pages, result);
        return static_cast<exret_t>(memory_error::VIRTUAL_MEMORY_CREATE_FAIL);
    }

    std::int64_t id = INVALID_ID;
    result = get_free_id(id);
    if (result != EXRET_SUCCESS) {
        release_heap(base, info.reserve_pages);
        std::print(stderr,
                   "create_heap_buffer: no free buffer slot "
                   "(max_heap_count={}, error_code={})\n",
                   MAX_HEAP_COUNT, result);
        return static_cast<exret_t>(memory_error::VIRTUAL_MEMORY_CREATE_FAIL);
    }

    assert(is_use_id(id) == false);
    heap_slot& slot = heap_slots_[id];
    slot.bytes = std::span<std::byte>{base, info.commit_pages * HEAP_PAGE_SIZE};
    slot.reserved_pages = info.reserve_pages;
    slot.committed_pages = info.commit_pages;
    change_buffer_used_state(id, true);
    out_id = id;

    return EXRET_SUCCESS;
}

void virtual_memory_system::reset_heap_slot(std::int64_t id) {
    heap_slots_[id] = {};
}

void virtual_memory_system::destroy_heap_buffer(std::int64_t id) {
    assert(id >= 0 && id < MAX_HEAP_COUNT);
    assert(is_use_id(id) == true);

    std::byte* addr = heap_slots_[id].bytes.data();
    assert(addr != nullptr);

    std::size_t pages = heap_slots_[id].reserved_pages;
    assert(pages > 0);

    release_heap(addr, pages);
    reset_heap_slot(id);

    change_buffer_used_state(id, false);
}

[[nodiscard]] exret_t
virtual_memory_system::resize_heap_buffer(std::int64_t id,
                                          std::size_t new_pages) {
    assert(is_use_id(id) == true);

    heap_slot& slot = heap_slots_[id];
    assert(new_pages <= slot.reserved_pages &&
           "heap size must fit within the reserved page count");

    if (new_pages > slot.committed_pages) {
        std::byte* addr =
            slot.bytes.data() + slot.committed_pages * HEAP_PAGE_SIZE;
        const std::size_t additional_pages = new_pages - slot.committed_pages;
        const exret_t result = commit_heap(addr, additional_pages);
        if (result != EXRET_SUCCESS) {
            return result;
        }
        slot.committed_pages = new_pages;
    }

    slot.bytes =
        std::span<std::byte>{slot.bytes.data(), new_pages * HEAP_PAGE_SIZE};

    return EXRET_SUCCESS;
}

[[nodiscard]] const std::span<std::byte>&
virtual_memory_system::get_heap_buffer(std::int64_t id) const& {
    assert(is_use_id(id) == true);
    return heap_slots_[id].bytes;
}

std::size_t virtual_memory_system::get_page_size() const {
    return HEAP_PAGE_SIZE;
}
} // namespace sky::memory
