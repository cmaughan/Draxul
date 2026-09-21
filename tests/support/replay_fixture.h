#pragma once

#include <draxul/nvim_protocol.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace draxul::tests
{

inline MpackValue i(int64_t value)
{
    return MpackValue::make_int(value);
}

inline MpackValue s(const std::string& value)
{
    return MpackValue::make_str(value);
}

inline MpackValue b(bool value)
{
    return MpackValue::make_bool(value);
}

inline MpackValue nil()
{
    return MpackValue::make_nil();
}

inline MpackValue arr(std::initializer_list<MpackValue> items)
{
    return MpackValue::make_array(std::vector<MpackValue>(items));
}

inline MpackValue map(std::initializer_list<std::pair<MpackValue, MpackValue>> items)
{
    return MpackValue::make_map(std::vector<std::pair<MpackValue, MpackValue>>(items));
}

inline MpackValue cell(
    const std::string& text,
    std::optional<int64_t> highlight = std::nullopt,
    std::optional<int64_t> repeat = std::nullopt)
{
    std::vector<MpackValue> values;
    values.push_back(s(text));
    if (highlight.has_value())
    {
        values.push_back(i(*highlight));
    }
    else if (repeat.has_value())
    {
        values.push_back(nil());
    }
    if (repeat.has_value())
    {
        values.push_back(i(*repeat));
    }
    return MpackValue::make_array(std::move(values));
}

inline MpackValue grid_line_batch(
    int64_t grid,
    int64_t row,
    int64_t col,
    std::initializer_list<MpackValue> cells)
{
    return arr({ i(grid), i(row), i(col), arr(cells) });
}

inline MpackValue redraw_event(const std::string& name, std::initializer_list<MpackValue> batches)
{
    std::vector<MpackValue> values;
    values.push_back(s(name));
    values.insert(values.end(), batches.begin(), batches.end());
    return MpackValue::make_array(std::move(values));
}

} // namespace draxul::tests
