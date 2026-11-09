#pragma once
#include <string>

namespace arena {
// Config is initial cash as decimal text; CSV has one column headed "price".
std::string run_arena(const std::string& csv, const std::string& config) noexcept;
}
