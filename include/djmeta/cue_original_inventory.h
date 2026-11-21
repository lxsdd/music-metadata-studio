#pragma once
// Complete local CUE source facts for read-only native comparison. No writes.
#include "djmeta/cue_metadata.h"
#include "djmeta/online_fields.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online {

inline bool is_local_cue_inventory(const FieldReviewRow& row) {
    const bool carrier = row.candidate.provider == "External CUE" ||
        row.candidate.provider == "Embedded CUE" ||
        row.candidate.provider == "External CUE / structure" ||
        row.candidate.provider == "Embedded CUE / structure";
    return carrier && (row.reason == "cue_inventory_read_only" ||
        row.reason == "cue_inventory_unqualified" ||
        row.reason == "local_cue_group");
}

// Keep EACH source occurrence, even unrecognized REM, with its own line.
// Never invent an editable target, alias, inheritance or provider source ID.
inline std::vector<FieldReviewRow> cue_original_inventory_rows(
    const CueMetadataInventory& inventory, std::string_view prefix = {}) {
    if (inventory.status == CueSyntaxStatus::Invalid ||
        inventory.encoding == CueTextEncoding::Unknown ||
        inventory.encoding == CueTextEncoding::UnsupportedUtf16)
        throw std::invalid_argument("Original CUE source is not readable.");
    constexpr std::size_t maximum_rows = 10000;
    const std::string carrier =
        inventory.carrier == CueCarrierKind::ExternalText
            ? "External CUE" : "Embedded CUE";
    std::vector<FieldReviewRow> result;
    auto add = [&](std::string label, std::string value,
                   std::size_t line_number, EvidenceScope scope,
                   bool structure = false) {
        if (result.size() >= maximum_rows)
            throw std::invalid_argument(
                "CUE original inventory exceeds 10,000 read-only rows.");
        FieldReviewRow row;
        row.field = std::string(prefix) + std::move(label);
        row.original_values.push_back(std::move(value));
        row.candidate.field = "LOCAL CUE ORIGINAL (not a tag proposal)";
        row.candidate.provider = carrier +
            (structure ? " / structure" : "");
        row.candidate.source_id = "cue-line:" + std::to_string(line_number);
        row.candidate.scope = scope;
        row.state = FieldReviewState::Unchanged;
        row.reason = inventory.status == CueSyntaxStatus::Parsed
            ? "cue_inventory_read_only" : "cue_inventory_unqualified";
        result.push_back(std::move(row));
    };
    for (const auto& field : inventory.globals)
        add("Album / " + field.name, field.value, field.line_number,
            EvidenceScope::Edition);
    for (const auto& track : inventory.tracks)
        for (const auto& field : track.local_fields)
            add("Track " + std::to_string(track.declared_track_number) +
                " (CUE ordinal " + std::to_string(track.ordinal + 1) +
                ", FILE #" + std::to_string(track.file_reference_index + 1) +
                ") / " + field.name, field.value, field.line_number,
                EvidenceScope::Recording);
    for (std::size_t i = 0; i < inventory.files.size(); ++i)
        add("FILE #" + std::to_string(i + 1) + " / FILE",
            inventory.files[i].filename, inventory.files[i].line_number,
            EvidenceScope::Edition, true);
    return result;
}

} // namespace djmeta::online
