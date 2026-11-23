#include "file.hpp"

#include <sys/types.h>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <print>
#include <unistd.h>

#include "types.hpp"

namespace sky::io {

[[nodiscard]] exret_t file_system::get_free_id(std::int64_t& out_id) const {
    assert(out_id == INVALID_ID);

    for (std::size_t word = 0; word < used_file_bitmap_.get_size(); ++word) {
        const std::uint64_t free_bits = ~used_file_bitmap_[word];
        if (free_bits == 0) {
            continue;
        }

        const std::size_t bit =
            static_cast<std::size_t>(std::countr_zero(free_bits));
        const std::size_t index = word * BITMAP_WORD_BITS + bit;
        if (index >= MAX_FILE_COUNT) {
            break;
        }

        out_id = static_cast<std::int64_t>(index);
        return EXRET_SUCCESS;
    }

    return static_cast<exret_t>(file_error::NO_FREE_FILE_SLOT);
}

[[nodiscard]] bool file_system::is_use_id(std::int64_t id) const {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_FILE_COUNT));

    const std::size_t index = static_cast<std::size_t>(id);
    const std::size_t word = index / BITMAP_WORD_BITS;
    const std::size_t bit = index % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;
    const bool is_used = (used_file_bitmap_[word] & mask) != 0;

    assert(is_used == true ? files_[id].fd >= 0 : files_[id].fd == -1);
    return is_used;
}

void file_system::change_file_used_state(std::int64_t id, bool is_use) {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_FILE_COUNT));

    const std::size_t index = static_cast<std::size_t>(id);
    const std::size_t word = index / BITMAP_WORD_BITS;
    const std::size_t bit = index % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;

    if (is_use) {
        used_file_bitmap_[word] |= mask;
    } else {
        used_file_bitmap_[word] &= ~mask;
    }
}

[[nodiscard]] exret_t
file_system::open_file(const char* path, std::int64_t& out_id,
                       file::flags flags, file::mode mode) {
    assert(path != nullptr);
    assert(out_id == INVALID_ID);

    std::int64_t id = INVALID_ID;
    const exret_t result = get_free_id(id);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "open_file: no free file slot "
                   "(max_file_count={}, error_code={})\n",
                   MAX_FILE_COUNT, result);
        return static_cast<exret_t>(file_error::OPEN_FILE_FAIL);
    }

    const int fd =
        ::open(path, static_cast<int>(flags), static_cast<mode_t>(mode));
    if (fd == -1) {
        std::perror("open");
        return static_cast<exret_t>(file_error::OPEN_FILE_FAIL);
    }

    assert(is_use_id(id) == false);
    files_[id].fd = fd;
    change_file_used_state(id, true);
    out_id = id;
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t
file_system::read_file(std::int64_t id, std::byte* dst, std::size_t size,
                       std::size_t& out_bytes) {
    assert(is_use_id(id) == true);
    assert(dst != nullptr || size == 0);
    assert(out_bytes == 0);

    const int fd = files_[id].fd;
    const ssize_t result = ::read(fd, dst, size);
    if (result == -1) {
        std::perror("read");
        return static_cast<exret_t>(file_error::READ_FILE_FAIL);
    }

    out_bytes = static_cast<std::size_t>(result);
    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t
file_system::write_file(std::int64_t id, const std::byte* src, std::size_t size,
                        std::size_t& out_bytes) {
    assert(is_use_id(id) == true);
    assert(src != nullptr || size == 0);
    assert(out_bytes == 0);

    const int fd = files_[id].fd;
    const ssize_t result = ::write(fd, src, size);
    if (result == -1) {
        std::perror("write");
        return static_cast<exret_t>(file_error::WRITE_FILE_FAIL);
    }

    out_bytes = static_cast<std::size_t>(result);
    return EXRET_SUCCESS;
}

void file_system::close_file(std::int64_t id) {
    assert(is_use_id(id) == true);

    file& slot = files_[id];
    const int result = ::close(slot.fd);
    if (result == -1) {
        std::perror("close");
    }

    slot.fd = -1;
    change_file_used_state(id, false);
}

} // namespace sky::io
