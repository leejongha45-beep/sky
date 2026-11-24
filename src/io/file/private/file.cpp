#include "file.hpp"

// clang-format off
#include <sys/types.h>
// clang-format on

#include <bit>
#include <cassert>
#include <climits> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/limits.h> // IWYU pragma: keep
#include <print>
#include <span>
#include <string_view>
#include <unistd.h>

#include "types.hpp"

namespace sky::io {

[[nodiscard]] file_system& file_system::get() {
    static file_system instance;
    return instance;
}

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

    const std::size_t idx = static_cast<std::size_t>(id);
    const std::size_t word = idx / BITMAP_WORD_BITS;
    const std::size_t bit = idx % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;
    const bool is_used = (used_file_bitmap_[word] & mask) != 0;

    assert(is_used == true ? files_[id].fd_ >= 0 : files_[id].fd_ == -1);
    return is_used;
}

void file_system::change_file_used_state(std::int64_t id, bool is_use) {
    assert(id >= 0 && id < static_cast<std::int64_t>(MAX_FILE_COUNT));

    const std::size_t idx = static_cast<std::size_t>(id);
    const std::size_t word = idx / BITMAP_WORD_BITS;
    const std::size_t bit = idx % BITMAP_WORD_BITS;
    const std::uint64_t mask = std::uint64_t{1} << bit;

    if (is_use) {
        used_file_bitmap_[word] |= mask;
    } else {
        used_file_bitmap_[word] &= ~mask;
    }
}

[[nodiscard]] exret_t file_system::open_file(const file::open_info& info,
                                             std::int64_t& out_id) {
    const std::string_view path = info.path;
    assert(path.data() != nullptr);
    assert(path.size() < PATH_MAX);
    assert(path.find('\0') == std::string_view::npos);
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

    char terminated_path[PATH_MAX];
    const std::size_t length = path.copy(terminated_path, path.size());
    terminated_path[length] = '\0';

    const int fd = ::open(terminated_path, static_cast<int>(info.flag),
                          static_cast<mode_t>(info.mode));
    if (fd == -1) {
        std::perror("open");
        return static_cast<exret_t>(file_error::OPEN_FILE_FAIL);
    }

    assert(is_use_id(id) == false);
    file& slot = files_[id];
    slot.fd_ = fd;
    slot.open_flag = info.flag;
    slot.create_mode = info.mode;
    std::memcpy(&slot.path[0], terminated_path, length + 1);
    change_file_used_state(id, true);
    out_id = id;

    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t file_system::read_file(std::int64_t id,
                                             std::span<std::byte> dst,
                                             std::size_t& out_bytes) {
    assert(is_use_id(id) == true);
    assert(dst.data() != nullptr || dst.empty());
    assert(out_bytes == 0);

    const int fd = files_[id].fd_;
    const ssize_t result = ::read(fd, dst.data(), dst.size());
    if (result == -1) {
        std::perror("read");
        return static_cast<exret_t>(file_error::READ_FILE_FAIL);
    }

    out_bytes = static_cast<std::size_t>(result);

    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t file_system::write_file(std::int64_t id,
                                              std::span<const std::byte> src,
                                              std::size_t& out_bytes) {
    assert(is_use_id(id) == true);
    assert(src.data() != nullptr || src.empty());
    assert(out_bytes == 0);

    const int fd = files_[id].fd_;
    const ssize_t result = ::write(fd, src.data(), src.size());
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
    const int result = ::close(slot.fd_);
    if (result == -1) {
        std::perror("close");
    }

    slot.fd_ = -1;
    slot.open_flag = file::flag::READ_ONLY;
    slot.create_mode = file::mode::NONE;
    slot.path[0] = '\0';
    change_file_used_state(id, false);
}

} // namespace sky::io
