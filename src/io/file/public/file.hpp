#pragma once

#include <cstddef>
#include <cstdint>
#include <fcntl.h>

#include "array.hpp"
#include "types.hpp"

namespace sky::io {

enum class file_error : exret_t {
    NO_FREE_FILE_SLOT = 1,
    OPEN_FILE_FAIL = 2,
    READ_FILE_FAIL = 3,
    WRITE_FILE_FAIL = 4,
    CLOSE_FILE_FAIL = 5,
};

class file final
{
public:
    file() = default;
    ~file() = default;

    file(const file&) = delete;
    file& operator=(const file&) = delete;
    file(file&&) = delete;
    file& operator=(file&&) = delete;

    enum class flags : int {
        READ_ONLY = O_RDONLY,
        WRITE_ONLY = O_WRONLY,
        READ_WRITE = O_RDWR,
        CREATE = O_CREAT,
        EXCLUSIVE = O_EXCL,
        TRUNCATE = O_TRUNC,
        APPEND = O_APPEND,
        CLOSE_ON_EXEC = O_CLOEXEC,
        DIRECTORY = O_DIRECTORY,
        NO_FOLLOW = O_NOFOLLOW,
        NON_BLOCKING = O_NONBLOCK,
        TEMPORARY = O_TMPFILE,
    };

    [[nodiscard]] friend constexpr flags operator|(flags left, flags right) {
        return static_cast<flags>(static_cast<int>(left) |
                                       static_cast<int>(right));
    }

    enum class mode : std::uint32_t {
        NONE = 0,
        OWNER_READ = 0400,
        OWNER_WRITE = 0200,
        OWNER_EXECUTE = 0100,
        GROUP_READ = 0040,
        GROUP_WRITE = 0020,
        GROUP_EXECUTE = 0010,
        OTHER_READ = 0004,
        OTHER_WRITE = 0002,
        OTHER_EXECUTE = 0001,
        SET_UID = 04000,
        SET_GID = 02000,
        STICKY = 01000,
    };

    [[nodiscard]] friend constexpr mode operator|(mode left, mode right) {
        return static_cast<mode>(static_cast<std::uint32_t>(left) |
                                      static_cast<std::uint32_t>(right));
    }

private:
    friend class file_system;

    int fd = -1;
};

class file_system final
{
public:
    file_system() = default;
    ~file_system() = default;

    file_system(const file_system&) = delete;
    file_system& operator=(const file_system&) = delete;
    file_system(file_system&&) = delete;
    file_system& operator=(file_system&&) = delete;

    [[nodiscard]] exret_t
    open_file(const char* path, std::int64_t& out_id,
              file::flags flags, file::mode mode);
    [[nodiscard]] exret_t read_file(std::int64_t id, std::byte* dst,
                                    std::size_t size, std::size_t& out_bytes);
    [[nodiscard]] exret_t write_file(std::int64_t id, const std::byte* src,
                                     std::size_t size, std::size_t& out_bytes);
    void close_file(std::int64_t id);

private:
    [[nodiscard]] exret_t get_free_id(std::int64_t& out_id) const;
    [[nodiscard]] bool is_use_id(std::int64_t id) const;
    void change_file_used_state(std::int64_t id, bool is_use);

    static constexpr std::size_t MAX_FILE_COUNT = 256;
    static constexpr std::size_t BITMAP_WORD_BITS = 64;
    static constexpr std::size_t BITMAP_WORD_COUNT =
        (MAX_FILE_COUNT + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;

    container::array<file, MAX_FILE_COUNT> files_ = {};
    container::array<std::uint64_t, BITMAP_WORD_COUNT> used_file_bitmap_ = {};
};

} // namespace sky::io
