#pragma once

#include <cstddef>

namespace djmeta {

// Exact one physical source: foobar /add followed by /context_command can
// report an empty recently cached TITLE despite the file having real tags.
// Force one authoritative async re-read before capturing the immutable
// read-only review. Never convert a large collection/virtual subsong into
// a bulk forced read, and never imply write authorization.
enum class SingleSourceTagLoad {
    CheckIfChanged,
    ForcePhysicalRefresh
};

constexpr SingleSourceTagLoad single_source_tag_load_policy(
    std::size_t selection_count, bool physical_subsong_zero) noexcept {
    return selection_count == 1 && physical_subsong_zero
        ? SingleSourceTagLoad::ForcePhysicalRefresh
        : SingleSourceTagLoad::CheckIfChanged;
}

} // namespace djmeta
