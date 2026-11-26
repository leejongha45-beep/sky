#pragma once

#include <cstddef>
#include <cstdint>

namespace sky {

using align_t = std::size_t;
constexpr align_t ALIGN_ANY = 0;

using exret_t = std::uint32_t;
constexpr exret_t EXRET_SUCCESS = 0;

constexpr std::int64_t INVALID_ID = -1;

} // namespace sky
