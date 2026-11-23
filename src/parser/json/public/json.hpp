#pragma once

#include <climits> // IWYU pragma: keep
#include <cstddef>
#include <limits>
#include <linux/limits.h> // IWYU pragma: keep
#include <string_view>

#include "array.hpp"
#include "types.hpp"

namespace sky::parser {

enum class json_error : exret_t {
    LOAD_FAIL = 2000,
    NOT_ENOUGH_SOURCE_SPACE = 2001,
    PARSE_FAIL = 2002,
    NOT_ENOUGH_NODE_SPACE = 2003,
    NESTING_LIMIT_EXCEEDED = 2004,
};

class json final
{
public:
    json() = default;
    ~json() = default;

    json(const json&) = delete;
    json& operator=(const json&) = delete;
    json(json&&) = delete;
    json& operator=(json&&) = delete;

    static constexpr std::size_t INVALID_NODE_INDEX =
        std::numeric_limits<std::size_t>::max();

    struct node final {
        enum class type {
            OBJECT,
            ARRAY,
            STRING,
            NUMBER,
            TRUE_VALUE,
            FALSE_VALUE,
            NULL_VALUE,
        };

        type type_ = type::NULL_VALUE;
        std::size_t offset = 0;
        std::size_t length = 0;
        std::size_t first_child = INVALID_NODE_INDEX;
        std::size_t next_sibling = INVALID_NODE_INDEX;
        std::size_t key_offset = 0;
        std::size_t key_length = 0;
    };

    [[nodiscard]] exret_t load(std::string_view path);
    [[nodiscard]] std::size_t get_root_index() const;
    [[nodiscard]] const node& get_node(std::size_t index) const&;
    const node& get_node(std::size_t) const&& = delete;
    [[nodiscard]] std::string_view get_node_text(std::size_t index) const&;
    std::string_view get_node_text(std::size_t) const&& = delete;
    [[nodiscard]] std::string_view get_key_text(std::size_t index) const&;
    std::string_view get_key_text(std::size_t) const&& = delete;

private:
    struct token final {
        enum class type {
            INVALID,
            END,
            OBJECT_OPEN,
            OBJECT_CLOSE,
            ARRAY_OPEN,
            ARRAY_CLOSE,
            COLON,
            COMMA,
            STRING,
            NUMBER,
            TRUE_VALUE,
            FALSE_VALUE,
            NULL_VALUE,
        };

        type type_ = type::INVALID;
        std::size_t offset = 0;
        std::size_t length = 0;
    };

    [[nodiscard]] static constexpr bool is_json_whitespace(char ch);
    [[nodiscard]] static constexpr bool is_json_structural_char(char ch);
    [[nodiscard]] static constexpr bool is_json_digit(char ch);
    [[nodiscard]] static constexpr bool is_hex_digit(char ch);

    void make_flat();
    [[nodiscard]] token next_token(std::size_t& cursor) const;
    [[nodiscard]] bool is_token_end(std::size_t cursor) const;
    [[nodiscard]] bool scan_literal(std::size_t& cursor,
                                    std::string_view suffix) const;
    [[nodiscard]] bool scan_digits(std::size_t& cursor) const;
    [[nodiscard]] bool scan_number(std::size_t& cursor) const;
    [[nodiscard]] bool scan_string(std::size_t& cursor) const;
    [[nodiscard]] bool scan_utf8_tail(unsigned char lead,
                                      std::size_t& cursor) const;
    [[nodiscard]] exret_t parse_value(token& current, std::size_t& cursor,
                                      std::size_t depth,
                                      std::size_t& out_index);
    [[nodiscard]] static constexpr bool is_container(node::type type);
    [[nodiscard]] exret_t parse_tree(token& current, std::size_t& cursor,
                                     std::size_t& out_index);

    static constexpr std::size_t MAX_JSON_SOURCE_BYTES = 1'048'576; // 1 MiB
    static constexpr std::size_t MAX_JSON_NODE_COUNT = 16'384;
    static constexpr std::size_t MAX_JSON_DEPTH = 256;

    container::array<char, MAX_JSON_SOURCE_BYTES> text_ = {};
    std::size_t text_size_ = 0;
    container::array<node, MAX_JSON_NODE_COUNT> nodes_ = {};
    std::size_t node_count_ = 0;
    std::size_t root_index_ = INVALID_NODE_INDEX;
};

} // namespace sky::parser
