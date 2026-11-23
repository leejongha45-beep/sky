#pragma once

#include <cassert>
#include <cstddef>

namespace sky::container {
template <typename T, std::size_t Cap>
class array final
{
    static_assert(Cap > 0);

public:
    array() = default;
    ~array() = default;

    array(const array&) = delete;
    array& operator=(const array&) = delete;
    array(array&&) = delete;
    array& operator=(array&&) = delete;

    [[nodiscard]] T& operator[](std::size_t idx) & {
        assert(idx < Cap);

        return vals_[idx];
    }

    [[nodiscard]] const T& operator[](std::size_t idx) const & {
        assert(idx < Cap);

        return vals_[idx];
    }

    const T& operator[](std::size_t) const && = delete;

    [[nodiscard]] constexpr std::size_t get_size() const {
        return Cap;
    }

private:
    T vals_[Cap] = {};
};
} // namespace sky::container
