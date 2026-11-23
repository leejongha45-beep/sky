#include "json.hpp"

#include <cassert>
#include <climits> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <linux/limits.h> // IWYU pragma: keep
#include <print>
#include <string_view>

#include "array.hpp"
#include "file.hpp"
#include "types.hpp"

namespace sky::parser {

[[nodiscard]] constexpr bool json::is_json_whitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

[[nodiscard]] constexpr bool json::is_json_structural_char(char ch) {
    return ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == ',' ||
           ch == ':';
}

[[nodiscard]] exret_t json::load(std::string_view path) {
    assert(path.data() != nullptr);
    assert(path.size() < PATH_MAX);
    assert(path.find('\0') == std::string_view::npos);
    assert(path.ends_with(".json"));

    text_size_ = 0;
    node_count_ = 0;
    root_index_ = INVALID_NODE_INDEX;

    io::file_system& fs = io::file_system::get();
    std::int64_t file_id = INVALID_ID;
    exret_t result = fs.open_file(path, file_id, io::file::flag::READ_ONLY,
                                  io::file::mode::NONE);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "json::load: open_file failed (path={}, error_code={})\n",
                   path, result);
        return static_cast<exret_t>(json_error::LOAD_FAIL);
    }

    std::size_t loaded_bytes = 0;
    while (loaded_bytes < MAX_JSON_SOURCE_BYTES) {
        std::size_t read_bytes = 0;
        std::byte* dst = reinterpret_cast<std::byte*>(&text_[loaded_bytes]);
        const std::size_t remaining = MAX_JSON_SOURCE_BYTES - loaded_bytes;
        result = fs.read_file(file_id, dst, remaining, read_bytes);
        if (result != EXRET_SUCCESS || read_bytes == 0) {
            break;
        }
        loaded_bytes += read_bytes;
    }

    std::size_t extra_bytes = 0;
    if (result == EXRET_SUCCESS && loaded_bytes == MAX_JSON_SOURCE_BYTES) {
        std::byte extra_byte{};
        result = fs.read_file(file_id, &extra_byte, 1, extra_bytes);
    }

    fs.close_file(file_id);
    if (result != EXRET_SUCCESS) {
        std::print(stderr,
                   "json::load: read_file failed (path={}, error_code={})\n",
                   path, result);
        return static_cast<exret_t>(json_error::LOAD_FAIL);
    }
    if (extra_bytes != 0) {
        std::print(stderr, "json::load: source exceeds {} bytes (path={})\n",
                   MAX_JSON_SOURCE_BYTES, path);
        return static_cast<exret_t>(json_error::NOT_ENOUGH_SOURCE_SPACE);
    }

    text_size_ = loaded_bytes;
    make_flat();

    std::size_t cursor = 0;
    token current = next_token(cursor);
    std::size_t root = INVALID_NODE_INDEX;
    result = parse_tree(current, cursor, root);
    if (result == EXRET_SUCCESS && current.type_ != token::type::END) {
        result = static_cast<exret_t>(json_error::PARSE_FAIL);
    }
    if (result != EXRET_SUCCESS) {
        text_size_ = 0;
        node_count_ = 0;
        std::print(stderr,
                   "json::load: parse failed (path={}, flat_offset={}, "
                   "error_code={})\n",
                   path, current.offset, result);
        return result;
    }

    root_index_ = root;

    return EXRET_SUCCESS;
}

[[nodiscard]] std::size_t json::get_root_index() const {
    return root_index_;
}

[[nodiscard]] const json::node& json::get_node(std::size_t index) const {
    assert(index < node_count_);
    return nodes_[index];
}

[[nodiscard]] std::string_view json::get_node_text(std::size_t index) const {
    const node& value = get_node(index);
    assert(value.offset < text_size_ &&
           value.length <= text_size_ - value.offset);
    return {&text_[value.offset], value.length};
}

[[nodiscard]] std::string_view json::get_key_text(std::size_t index) const {
    const node& value = get_node(index);
    if (value.key_length == 0) {
        return {};
    }
    assert(value.key_offset < text_size_ &&
           value.key_length <= text_size_ - value.key_offset);
    return {&text_[value.key_offset], value.key_length};
}

void json::make_flat() {
    assert(text_size_ <= text_.get_size());

    std::size_t input_index = 0;
    std::size_t output_index = 0;
    bool in_string = false;
    bool escaped = false;

    while (input_index < text_size_) {
        assert(output_index <= input_index);

        const char ch = text_[input_index];

        if (in_string == false && is_json_whitespace(ch)) {
            const std::size_t whitespace_start = input_index;
            do {
                ++input_index;
            } while (input_index < text_size_ &&
                     is_json_whitespace(text_[input_index]));

            const bool follows_boundary =
                output_index == 0 ||
                is_json_structural_char(text_[output_index - 1]);
            const bool precedes_boundary =
                input_index == text_size_ ||
                is_json_structural_char(text_[input_index]);

            if (follows_boundary == false && precedes_boundary == false) {
                for (std::size_t index = whitespace_start; index < input_index;
                     ++index) {
                    text_[output_index++] = text_[index];
                }
            }
            continue;
        }

        text_[output_index++] = ch;
        ++input_index;
        if (in_string == true) {
            if (escaped == true) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
        } else if (ch == '"') {
            in_string = true;
        }
    }

    text_size_ = output_index;
}

[[nodiscard]] constexpr bool json::is_json_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool json::is_hex_digit(char ch) {
    return is_json_digit(ch) || (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

[[nodiscard]] bool json::is_token_end(std::size_t cursor) const {
    assert(cursor <= text_size_);
    return cursor == text_size_ || is_json_whitespace(text_[cursor]) ||
           is_json_structural_char(text_[cursor]);
}

[[nodiscard]] bool json::scan_literal(std::size_t& cursor,
                                      std::string_view suffix) const {
    assert(cursor <= text_size_);

    if (suffix.size() > text_size_ - cursor) {
        return false;
    }
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        if (text_[cursor + index] != suffix[index]) {
            return false;
        }
    }
    cursor += suffix.size();

    return is_token_end(cursor);
}

[[nodiscard]] bool json::scan_digits(std::size_t& cursor) const {
    const std::size_t start = cursor;
    while (cursor < text_size_ && is_json_digit(text_[cursor])) {
        ++cursor;
    }

    return cursor != start;
}

[[nodiscard]] bool json::scan_number(std::size_t& cursor) const {
    assert(cursor < text_size_);

    if (text_[cursor] == '-') {
        ++cursor;
    }
    if (cursor == text_size_) {
        return false;
    }
    if (text_[cursor] == '0') {
        ++cursor;
    } else if (scan_digits(cursor) == false) {
        return false;
    }

    if (cursor < text_size_ && text_[cursor] == '.') {
        ++cursor;
        if (scan_digits(cursor) == false) {
            return false;
        }
    }

    if (cursor < text_size_ && (text_[cursor] == 'e' || text_[cursor] == 'E')) {
        ++cursor;
        if (cursor < text_size_ &&
            (text_[cursor] == '+' || text_[cursor] == '-')) {
            ++cursor;
        }
        if (scan_digits(cursor) == false) {
            return false;
        }
    }

    return is_token_end(cursor);
}

[[nodiscard]] bool json::scan_utf8_tail(unsigned char lead,
                                        std::size_t& cursor) const {
    assert(cursor <= text_size_);

    std::size_t count = 0;
    unsigned char first_min = 0x80;
    unsigned char first_max = 0xbf;
    if (lead >= 0xc2 && lead <= 0xdf) {
        count = 1;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        count = 2;
        if (lead == 0xe0) {
            first_min = 0xa0;
        } else if (lead == 0xed) {
            first_max = 0x9f;
        }
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        count = 3;
        if (lead == 0xf0) {
            first_min = 0x90;
        } else if (lead == 0xf4) {
            first_max = 0x8f;
        }
    } else {
        return false;
    }

    if (count > text_size_ - cursor) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(text_[cursor]);
    if (first < first_min || first > first_max) {
        return false;
    }
    for (std::size_t index = 1; index < count; ++index) {
        const unsigned char tail =
            static_cast<unsigned char>(text_[cursor + index]);
        if (tail < 0x80 || tail > 0xbf) {
            return false;
        }
    }

    cursor += count;

    return true;
}

[[nodiscard]] bool json::scan_string(std::size_t& cursor) const {
    while (cursor < text_size_) {
        const unsigned char ch = static_cast<unsigned char>(text_[cursor++]);
        if (ch == '"') {
            return true;
        }
        if (ch < 0x20) {
            return false;
        }
        if (ch >= 0x80 && scan_utf8_tail(ch, cursor) == false) {
            return false;
        }
        if (ch != '\\') {
            continue;
        }
        if (cursor == text_size_) {
            return false;
        }

        switch (text_[cursor++]) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            break;
        case 'u':
            if (text_size_ - cursor < 4) {
                return false;
            }
            for (std::size_t index = 0; index < 4; ++index) {
                if (is_hex_digit(text_[cursor++]) == false) {
                    return false;
                }
            }
            break;
        default:
            return false;
        }
    }

    return false;
}

[[nodiscard]] json::token json::next_token(std::size_t& cursor) const {
    assert(cursor <= text_size_);
    while (cursor < text_size_ && is_json_whitespace(text_[cursor])) {
        ++cursor;
    }
    if (cursor == text_size_) {
        return {token::type::END, cursor, 0};
    }

    const std::size_t start = cursor;
    const char ch = text_[cursor++];
    token::type type = token::type::INVALID;
    switch (ch) {
    case '{':
        type = token::type::OBJECT_OPEN;
        break;
    case '}':
        type = token::type::OBJECT_CLOSE;
        break;
    case '[':
        type = token::type::ARRAY_OPEN;
        break;
    case ']':
        type = token::type::ARRAY_CLOSE;
        break;
    case ':':
        type = token::type::COLON;
        break;
    case ',':
        type = token::type::COMMA;
        break;
    case '"':
        if (scan_string(cursor) == true) {
            type = token::type::STRING;
        }
        break;
    case 't':
        if (scan_literal(cursor, "rue") == true) {
            type = token::type::TRUE_VALUE;
        }
        break;
    case 'f':
        if (scan_literal(cursor, "alse") == true) {
            type = token::type::FALSE_VALUE;
        }
        break;
    case 'n':
        if (scan_literal(cursor, "ull") == true) {
            type = token::type::NULL_VALUE;
        }
        break;
    default:
        if (ch == '-' || is_json_digit(ch)) {
            cursor = start;
            if (scan_number(cursor) == true) {
                type = token::type::NUMBER;
            }
        }
        break;
    }

    return {type, start, cursor - start};
}

[[nodiscard]] constexpr bool json::is_container(node::type type) {
    return type == node::type::OBJECT || type == node::type::ARRAY;
}

[[nodiscard]] exret_t json::parse_value(token& current, std::size_t& cursor,
                                        std::size_t depth,
                                        std::size_t& out_index) {
    assert(out_index == INVALID_NODE_INDEX);
    assert(current.offset <= cursor && cursor <= text_size_ &&
           current.length == cursor - current.offset);
    node::type type = node::type::NULL_VALUE;
    switch (current.type_) {
    case token::type::OBJECT_OPEN:
        type = node::type::OBJECT;
        break;
    case token::type::ARRAY_OPEN:
        type = node::type::ARRAY;
        break;
    case token::type::STRING:
        type = node::type::STRING;
        break;
    case token::type::NUMBER:
        type = node::type::NUMBER;
        break;
    case token::type::TRUE_VALUE:
        type = node::type::TRUE_VALUE;
        break;
    case token::type::FALSE_VALUE:
        type = node::type::FALSE_VALUE;
        break;
    case token::type::NULL_VALUE:
        break;
    default:
        return static_cast<exret_t>(json_error::PARSE_FAIL);
    }

    if (is_container(type) && depth >= MAX_JSON_DEPTH) {
        return static_cast<exret_t>(json_error::NESTING_LIMIT_EXCEEDED);
    }
    if (node_count_ == nodes_.get_size()) {
        return static_cast<exret_t>(json_error::NOT_ENOUGH_NODE_SPACE);
    }

    const std::size_t index = node_count_++;
    nodes_[index] = {
        .type_ = type, .offset = current.offset, .length = current.length};
    current = next_token(cursor);

    out_index = index;

    return EXRET_SUCCESS;
}

[[nodiscard]] exret_t json::parse_tree(token& current, std::size_t& cursor,
                                       std::size_t& out_index) {
    assert(out_index == INVALID_NODE_INDEX);

    std::size_t root_index = INVALID_NODE_INDEX;
    const exret_t root_result = parse_value(current, cursor, 0, root_index);
    if (root_result != EXRET_SUCCESS) {
        return root_result;
    }
    assert(root_index < node_count_);
    if (is_container(nodes_[root_index].type_) == false) {
        out_index = root_index;
        return EXRET_SUCCESS;
    }

    struct parse_frame final {
        std::size_t parent_index = INVALID_NODE_INDEX;
        std::size_t prev_child_index = INVALID_NODE_INDEX;
    };

    container::array<parse_frame, MAX_JSON_DEPTH> frames = {};
    std::size_t depth = 1;
    frames[0].parent_index = root_index;

    while (depth != 0) {
        parse_frame& frame = frames[depth - 1];
        assert(frame.parent_index < node_count_);
        node& parent = nodes_[frame.parent_index];
        assert(is_container(parent.type_) == true);

        const bool is_object = parent.type_ == node::type::OBJECT;
        const token::type end =
            is_object ? token::type::OBJECT_CLOSE : token::type::ARRAY_CLOSE;
        if (current.type_ == end) {
            assert(parent.offset < current.offset &&
                   current.offset < text_size_);
            assert(current.length == 1);
            parent.length = current.offset + current.length - parent.offset;
            current = next_token(cursor);
            --depth;
            continue;
        }

        if (frame.prev_child_index != INVALID_NODE_INDEX) {
            if (current.type_ != token::type::COMMA) {
                return static_cast<exret_t>(json_error::PARSE_FAIL);
            }
            current = next_token(cursor);
        }

        token key{};
        if (is_object == true) {
            if (current.type_ != token::type::STRING) {
                return static_cast<exret_t>(json_error::PARSE_FAIL);
            }
            key = current;
            current = next_token(cursor);
            if (current.type_ != token::type::COLON) {
                return static_cast<exret_t>(json_error::PARSE_FAIL);
            }
            current = next_token(cursor);
        }

        std::size_t child_index = INVALID_NODE_INDEX;
        const exret_t result = parse_value(current, cursor, depth, child_index);
        if (result != EXRET_SUCCESS) {
            return result;
        }

        assert(frame.parent_index < child_index && child_index < node_count_);
        node& child = nodes_[child_index];
        if (is_object == true) {
            child.key_offset = key.offset;
            child.key_length = key.length;
        }
        if (frame.prev_child_index == INVALID_NODE_INDEX) {
            parent.first_child = child_index;
        } else {
            assert(frame.prev_child_index < child_index);
            assert(nodes_[frame.prev_child_index].next_sibling ==
                   INVALID_NODE_INDEX);
            nodes_[frame.prev_child_index].next_sibling = child_index;
        }
        frame.prev_child_index = child_index;

        if (is_container(child.type_) == true) {
            assert(depth < frames.get_size());
            frames[depth] = {.parent_index = child_index};
            ++depth;
        }
    }

    out_index = root_index;

    return EXRET_SUCCESS;
}
} // namespace sky::parser
