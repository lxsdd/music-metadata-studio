#pragma once
// Read-only sibling external CUE target classification for foobar previews.
// The result is NOT commit authorization; a writer must revalidate everything.
#include "djmeta/precision_cue_external_create.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace djmeta {

enum class PrecisionCueExistingTargetState {
    Absent,
    Identical,
    Different,
    Unqualified
};
struct PrecisionCueExistingTargetInspection {
    PrecisionCueExistingTargetState state = PrecisionCueExistingTargetState::Unqualified;
    std::string diagnostic;
};

inline PrecisionCueExistingTargetInspection inspect_precision_cue_existing_target(
    const PrecisionCuePlan& plan, const std::filesystem::path& selected_physical_mp3,
    std::string_view mp3_basename) {
    using State = PrecisionCueExistingTargetState;
    namespace fs = std::filesystem;
    try {
        const auto proposed = plan_precision_cue_external_export(plan, mp3_basename);
        if (!proposed.eligible)
            return {State::Unqualified, "CUE_EXPORT_PLAN_INVALID"};
        const auto parent = selected_physical_mp3.parent_path();
        std::error_code ec;
        if (!parent.is_absolute() || parent != parent.lexically_normal() ||
            fs::canonical(parent, ec) != parent || ec ||
            !precision_cue_external_detail::physical_source_snapshot(
                selected_physical_mp3, parent, mp3_basename))
            return {State::Unqualified, "CUE_SOURCE_NOT_QUALIFIED"};
        const auto target = parent /
            precision_cue_external_detail::utf8_path(proposed.target_cue_basename);
        if (target.parent_path() != parent)
            return {State::Unqualified, "CUE_TARGET_PATH_INVALID"};
        const auto status = fs::symlink_status(target, ec);
        if (ec == std::errc::no_such_file_or_directory ||
            (!ec && status.type() == fs::file_type::not_found))
            return {State::Absent, "CUE_TARGET_ABSENT_AT_PREVIEW"};
        if (ec || status.type() != fs::file_type::regular)
            return {State::Unqualified, "CUE_TARGET_SYMLINK_OR_SPECIAL"};
        const auto size = fs::file_size(target, ec);
        if (ec || size > 1024 * 1024)
            return {State::Unqualified, "CUE_EXISTING_CONTENT_UNQUALIFIED"};
        std::ifstream input(target, std::ios::binary);
        if (!input) return {State::Unqualified, "CUE_EXISTING_CONTENT_UNREADABLE"};
        std::string existing(static_cast<std::size_t>(size), '\0');
        if (!input.read(existing.data(), static_cast<std::streamsize>(existing.size())) ||
            input.peek() != std::char_traits<char>::eof())
            return {State::Unqualified, "CUE_EXISTING_CONTENT_UNREADABLE"};
        if (existing == plan.external_cue_utf8)
            return {State::Identical, "CUE_IDENTICAL_NOOP_ONLY"};
        return {State::Different, "CUE_CONFLICT_NO_OVERWRITE"};
    } catch (const std::exception&) {
        return {State::Unqualified, "CUE_PREVIEW_TARGET_INSPECTION_FAILED"};
    }
}

} // namespace djmeta
