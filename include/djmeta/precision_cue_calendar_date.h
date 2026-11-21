#pragma once
// Optional release-date metadata is never timing evidence. Accept only
// explicit ISO calendar precision (year, month or day); do not invent a day.
// Full catalog dates are supplied by provider parsers, while a user-entered
// year/month remains unverified REVIEW metadata in the CUE header.
#include <string_view>

namespace djmeta {
inline bool precision_cue_valid_calendar_date(std::string_view value) noexcept {
    if (value.empty()) return true;
    if (value.size() != 4 && value.size() != 7 && value.size() != 10)
        return false;
    const auto digit = [&](std::size_t index) -> int {
        const char c = value[index];
        return c >= '0' && c <= '9' ? c - '0' : -1;
    };
    const auto number = [&](std::size_t index) -> int {
        const int a = digit(index), b = digit(index + 1);
        return a < 0 || b < 0 ? -1 : a * 10 + b;
    };
    const int y0 = number(0), y1 = number(2);
    if (y0 < 0 || y1 < 0) return false;
    const int year = y0 * 100 + y1;
    if (year == 0) return false;
    if (value.size() == 4) return true;
    if (value[4] != '-') return false;
    const int month = number(5);
    if (month < 1 || month > 12) return false;
    if (value.size() == 7) return true;
    if (value[7] != '-') return false;
    const int day = number(8);
    constexpr int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day >= 1 && day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}
// Partial ISO dates are compatible only when all known fields agree.
// A shared year is not a claim that the unknown month/day were verified.
inline bool precision_cue_calendar_dates_compatible(
    std::string_view a, std::string_view b) noexcept {
    if (!precision_cue_valid_calendar_date(a) ||
        !precision_cue_valid_calendar_date(b)) return false;
    if (a.empty() || b.empty()) return true;
    const auto length = a.size() < b.size() ? a.size() : b.size();
    return a.substr(0, length) == b.substr(0, length);
}

} // namespace djmeta
