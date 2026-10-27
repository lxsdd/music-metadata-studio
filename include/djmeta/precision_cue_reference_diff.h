#pragma once
// Strict, read-only comparison of an independently supplied external CUE
// against generated 75-fps positions. A matching timeline never establishes
// edition/mastering identity, and NEVER authorizes external/embedded writes.
#include "djmeta/external_cue.h"
#include "djmeta/precision_cue_boundary_audit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta {

enum class PrecisionCueReferenceDiffState {
    ExactFramesPreviewOnly,
    WithinOneFramePreviewOnly,
    ReviewRequired,
    Unqualified
};

// Read-only per-track structure from the supplied reference CUE. The
// difference between INDEX 00 and INDEX 01 is expressed in exact CUE frames,
// not inferred audio samples. No field is a writer authorization.
struct PrecisionCueReferenceTrackStructure {
    unsigned track_number = 0;
    std::optional<std::int64_t> index_00_frame;
    std::optional<std::int64_t> index_00_to_01_frames;
    std::optional<std::int64_t> pregap_frames;
    std::optional<std::int64_t> postgap_frames;
};

struct PrecisionCueReferenceDiff {
    PrecisionCueReferenceDiffState state = PrecisionCueReferenceDiffState::Unqualified;
    PrecisionCueAuditResult audit;
    bool extra_index_or_pregap = false;
    // Explicit reference TITLE/PERFORMER and album REM GENRE/DATE are
    // untrusted textual claims, not audio/edition proof. A conflicting or
    // duplicate supplied value forces REVIEW even when INDEX 01 agrees.
    // Missing optional text does not imply a mismatch.
    bool album_metadata_conflict = false;
    bool metadata_conflict = false;
    std::vector<bool> track_metadata_conflicts;
    // Populated only after complete syntax and timeline validation.
    // Preserve the actual affected track while keeping disc-wide REVIEW.
    std::vector<PrecisionCueReferenceTrackStructure> track_structure;
    // Stable, path-free diagnostics; do not log user-provided CUE text.
    const char* diagnostic = "CUE_REFERENCE_UNQUALIFIED";
};

namespace precision_cue_reference_detail {
inline bool whitespace(char c) { return c == ' ' || c == '\t'; }

inline std::string_view trim(std::string_view value) {
    while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
    while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
    return value;
}

inline std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    while (!line.empty()) {
        line = trim(line);
        if (line.empty()) break;
        const auto end = line.find_first_of(" \t");
        if (end == std::string_view::npos) {
            out.push_back(line);
            break;
        }
        out.push_back(line.substr(0, end));
        line.remove_prefix(end);
    }
    return out;
}

inline bool decimal(std::string_view token, unsigned& value,
                    unsigned limit, std::size_t max_digits) {
    if (token.empty() || token.size() > max_digits) return false;
    unsigned n = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + static_cast<unsigned>(c - '0');
        if (n > limit) return false;
    }
    value = n;
    return true;
}

inline bool cue_time(std::string_view token, std::int64_t& frames) {
    const auto first = token.find(':');
    if (first == std::string_view::npos) return false;
    const auto second = token.find(':', first + 1);
    if (second == std::string_view::npos ||
        token.find(':', second + 1) != std::string_view::npos) return false;
    const auto mm = token.substr(0, first);
    const auto ss = token.substr(first + 1, second - first - 1);
    const auto ff = token.substr(second + 1);
    if (mm.size() < 2 || ss.size() != 2 || ff.size() != 2) return false;
    unsigned minutes = 0, seconds = 0, sector = 0;
    if (!decimal(mm, minutes, 1440, 4) ||
        !decimal(ss, seconds, 59, 2) ||
        !decimal(ff, sector, 74, 2)) return false;
    frames = (static_cast<std::int64_t>(minutes) * 60 + seconds) * 75 + sector;
    return frames <= 24LL * 60 * 60 * 75;
}
// Compare only metadata that the reference explicitly declares. The
// generator's canonical lines are the comparison baseline, not a provider
// attestation. Exact text is advisory; different quoting/case is REVIEW,
// never an automatic edition rejection or a write permission.
struct ReferenceMetadataField {
    std::string_view expected;
    bool observed = false;
};
struct ReferenceMetadataScope {
    ReferenceMetadataField title;
    ReferenceMetadataField performer;
    ReferenceMetadataField genre;
    ReferenceMetadataField date;
};
inline ReferenceMetadataField* metadata_field(
    ReferenceMetadataScope& scope, std::string_view line,
    std::string_view op) {
    if (op == "TITLE") return &scope.title;
    if (op == "PERFORMER") return &scope.performer;
    if (op != "REM") return nullptr;
    const auto rest = trim(line.substr(3));
    const auto end = rest.find_first_of(" \t");
    const auto name = rest.substr(0, end);
    if (name == "GENRE") return &scope.genre;
    if (name == "DATE") return &scope.date;
    return nullptr;
}
inline bool expected_metadata_scopes(
    std::string_view generated,
    std::vector<ReferenceMetadataScope>& scopes) {
    std::size_t track = 0, cursor = 0;
    while (cursor < generated.size()) {
        const auto end = generated.find('\n', cursor);
        auto line = trim(generated.substr(cursor, end == std::string_view::npos
            ? generated.size() - cursor : end - cursor));
        cursor = end == std::string_view::npos ? generated.size() : end + 1;
        if (line.empty()) continue;
        const auto sep = line.find_first_of(" \t");
        const auto op = line.substr(0, sep);
        if (op == "TRACK") {
            if (++track >= scopes.size()) return false;
            continue;
        }
        if (auto* field = metadata_field(scopes[track], line, op)) {
            if (!field->expected.empty()) return false;
            field->expected = line;
        }
    }
    if (track + 1 != scopes.size() ||
        scopes[0].title.expected.empty() ||
        scopes[0].performer.expected.empty()) return false;
    for (std::size_t i = 1; i < scopes.size(); ++i)
        if (scopes[i].title.expected.empty() ||
            scopes[i].performer.expected.empty()) return false;
    return true;
}
} // namespace precision_cue_reference_detail

// Input bytes may come from an independently read, existing sibling CUE.
// This parser intentionally accepts only a bounded single-FILE AUDIO subset:
// unknown cue commands, duplicate/missing indices, invalid times, wrong
// filename/type and misleading extra file references fail closed.
// INDEX 00 and gap commands can be inspected but NEVER counted as exact
// uncomplicated track boundaries. No filesystem or host SDK APIs are called.
inline PrecisionCueReferenceDiff compare_precision_cue_external_reference(
    const PrecisionCuePlan& generated,
    std::string_view reference_cue_bytes,
    std::string_view expected_mp3_basename) {
    using namespace precision_cue_reference_detail;
    PrecisionCueReferenceDiff out;
    if (reference_cue_bytes.empty() ||
        reference_cue_bytes.size() > 1024 * 1024 ||
        generated.boundaries.empty() || generated.boundaries.size() > 99 ||
        generated.total_duration_ms <= 0 ||
        generated.total_duration_ms > 24LL * 60 * 60 * 1000)
        return out;
    const auto total_frames = milliseconds_to_cue_frame(generated.total_duration_ms);
    std::vector<ReferenceMetadataScope> metadata(generated.boundaries.size() + 1);
    if (!expected_metadata_scopes(generated.external_cue_utf8, metadata))
        return out;
    const auto inventory = inspect_external_cue(reference_cue_bytes);
    if (inventory.status != CueSyntaxStatus::Parsed ||
        inventory.references.size() != 1 ||
        inventory.references[0].filename != expected_mp3_basename ||
        (inventory.references[0].file_type != "WAVE" &&
         inventory.references[0].file_type != "MP3")) {
        out.diagnostic = "CUE_REFERENCE_FILE_OR_ENCODING_MISMATCH";
        return out;
    }
    auto input = reference_cue_bytes;
    if (inventory.encoding == CueTextEncoding::Utf8Bom &&
        input.size() >= 3) input.remove_prefix(3);

    std::vector<PrecisionCueReferenceBoundary> references;
    std::vector<PrecisionCueReferenceTrackStructure> structures;
    bool album_metadata_conflict = false;
    std::vector<bool> track_metadata_conflicts(generated.boundaries.size(), false);
    bool file_seen = false;
    bool index_01_seen = false;
    bool index_00_seen = false;
    bool pregap_seen = false;
    bool postgap_seen = false;
    std::int64_t index_00_frame = -1;
    std::int64_t last_index_01_frame = -1;
    bool extra_index = false;
    unsigned current_track = 0;
    std::size_t line_count = 0;
    std::size_t cursor = 0;
    while (cursor < input.size()) {
        if (++line_count > 2048) return out;
        const auto end = input.find('\n', cursor);
        auto line = input.substr(cursor, end == std::string_view::npos
            ? input.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? input.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.find('\r') != std::string_view::npos) return out;
        line = trim(line);
        if (line.empty()) continue;
        const auto sep = line.find_first_of(" \t");
        const auto op = line.substr(0, sep);
        if (op == "FILE") {
            if (file_seen || current_track != 0) return out;
            file_seen = true;
        } else if (op == "TRACK") {
            const auto tokens = words(line);
            if (!file_seen || tokens.size() != 3 ||
                tokens[2] != "AUDIO" ||
                current_track >= generated.boundaries.size() ||
                (current_track != 0 && !index_01_seen)) return out;
            unsigned number = 0;
            if (tokens[1].size() != 2 ||
                !decimal(tokens[1], number, 99, 2) ||
                number != current_track + 1) return out;
            ++current_track;
            structures.emplace_back();
            structures.back().track_number = current_track;
            index_01_seen = false;
            index_00_seen = false;
            pregap_seen = false;
            postgap_seen = false;
            index_00_frame = -1;
        } else if (op == "INDEX") {
            const auto tokens = words(line);
            if (!file_seen || current_track == 0 || tokens.size() != 3)
                return out;
            std::int64_t frame = -1;
            if (!cue_time(tokens[2], frame) || frame >= total_frames) {
                out.diagnostic = "CUE_REFERENCE_INDEX_OUTSIDE_DISC_CLOCK";
                return out;
            }
            if (tokens[1] == "00" && !index_00_seen && !index_01_seen &&
                !pregap_seen && frame >= last_index_01_frame) {
                index_00_seen = true;
                index_00_frame = frame;
                structures.back().index_00_frame = frame;
                extra_index = true;
            } else if (tokens[1] == "01" && !index_01_seen &&
                       (!index_00_seen || frame >= index_00_frame) &&
                       (current_track == 1 || frame > last_index_01_frame)) {
                index_01_seen = true;
                last_index_01_frame = frame;
                if (index_00_seen)
                    structures.back().index_00_to_01_frames = frame - index_00_frame;
                references.push_back({current_track, frame});
            } else {
                out.diagnostic = "CUE_REFERENCE_INVALID_INDEX_OR_GAP_ORDER";
                return out;
            }
        } else if (op == "REM" || op == "TITLE" || op == "PERFORMER" ||
                   op == "SONGWRITER" || op == "ISRC" ||
                   op == "CATALOG" || op == "FLAGS") {
            // An explicitly conflicting or duplicate metadata claim is
            // review evidence, not an edition/mastering match. Absent
            // optional text is allowed; unrelated REM directives are not
            // treated as authenticated release facts.
            if (auto* field = metadata_field(metadata[current_track], line, op)) {
                const bool conflict = field->observed || field->expected != line;
                field->observed = true;
                if (conflict) {
                    if (current_track == 0) album_metadata_conflict = true;
                    else track_metadata_conflicts[current_track - 1] = true;
                }
            }
        } else if (op == "PREGAP" || op == "POSTGAP") {
            if (current_track == 0) return out;
            const auto tokens = words(line);
            std::int64_t ignored = -1;
            if (tokens.size() != 2 || !cue_time(tokens[1], ignored)) return out;
            // Virtual gaps remain review-only; a gap at/after the entire
            // physical disc duration cannot be trusted as a bounded cue.
            if (ignored >= total_frames) {
                out.diagnostic = "CUE_REFERENCE_GAP_OUTSIDE_DISC_CLOCK";
                return out;
            }
            // PREGAP is pre-INDEX 01 virtual silence, POSTGAP follows
            // INDEX 01. Repetition or simultaneous INDEX 00/PREGAP is
            // ambiguous; never label malformed structural data as exact.
            if (op == "PREGAP") {
                if (index_01_seen || index_00_seen || pregap_seen) {
                    out.diagnostic = "CUE_REFERENCE_INVALID_INDEX_OR_GAP_ORDER";
                    return out;
                }
                pregap_seen = true;
                structures.back().pregap_frames = ignored;
            } else {
                if (!index_01_seen || postgap_seen) {
                    out.diagnostic = "CUE_REFERENCE_INVALID_INDEX_OR_GAP_ORDER";
                    return out;
                }
                postgap_seen = true;
                structures.back().postgap_frames = ignored;
            }
            extra_index = true;
        } else {
            return out;
        }
    }
    if (!file_seen || current_track != generated.boundaries.size() ||
        !index_01_seen || references.size() != current_track)
        return out;
    out.audit = audit_precision_cue_boundaries(generated, references);
    if (out.audit.state == PrecisionCueAuditState::Rejected) return out;
    out.track_structure = std::move(structures);
    out.extra_index_or_pregap = extra_index;
    out.album_metadata_conflict = album_metadata_conflict;
    out.track_metadata_conflicts = std::move(track_metadata_conflicts);
    out.metadata_conflict = album_metadata_conflict ||
        std::any_of(out.track_metadata_conflicts.begin(),
                    out.track_metadata_conflicts.end(),
                    [](bool conflict) { return conflict; });
    if (extra_index || out.metadata_conflict ||
        out.audit.state == PrecisionCueAuditState::Review) {
        out.state = PrecisionCueReferenceDiffState::ReviewRequired;
        out.diagnostic = extra_index
            ? "CUE_REFERENCE_EXTRA_INDEX_OR_GAP_REQUIRES_REVIEW"
            : out.metadata_conflict
                ? "CUE_REFERENCE_TEXT_METADATA_CONFLICT_REQUIRES_REVIEW"
                : "CUE_REFERENCE_BOUNDARY_DIFFERENCE_REQUIRES_REVIEW";
    } else if (out.audit.state == PrecisionCueAuditState::WithinOneFrame) {
        out.state = PrecisionCueReferenceDiffState::WithinOneFramePreviewOnly;
        out.diagnostic = "CUE_REFERENCE_WITHIN_ONE_FRAME_NOT_AUDIO_PROOF";
    } else {
        out.state = PrecisionCueReferenceDiffState::ExactFramesPreviewOnly;
        out.diagnostic = "CUE_REFERENCE_EXACT_FRAMES_NOT_EDITION_PROOF";
    }
    return out;
}

} // namespace djmeta
