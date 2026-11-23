#pragma once

namespace sky::parser {

class json final
{
public:
    json() = default;
    ~json() = default;

    json(const json&) = delete;
    json& operator=(const json&) = delete;
    json(json&&) = delete;
    json& operator=(json&&) = delete;

private:
};

} // namespace sky::parser
