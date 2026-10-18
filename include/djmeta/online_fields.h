#pragma once

// Read-only UI-facing comparison of local metadata vectors and provider
// evidence. No automatic source selection, deletion, or write authorization.
#include "djmeta/normalizer.h"
#include "djmeta/online_release.h"
#include "djmeta/structural_guard.h"

#include <cctype>
#include <cstddef>
#include <map>
#include <string>
#include <tuple>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online {

enum class FieldReviewState { Unchanged, NeedsReview, Blocked };

inline constexpr std::size_t no_local_field = static_cast<std::size_t>(-1);

struct FieldReviewRow {
    std::size_t evidence_index = 0;
    std::size_t local_field_index = no_local_field;
    std::string field; // Exact input spelling, never a synthesized alias.
    std::vector<std::string> original_values;
    FieldEvidence candidate; // Complete vectors, provider and date provenance.
    FieldReviewState state = FieldReviewState::Blocked;
    std::string reason;
};

inline std::string ascii_upper_field(std::string_view name) {
    std::string result;
    for (unsigned char c : name) {
        if (c == 0 || c < 0x20 || c == 0x7f) return {};
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - ('a' - 'A'));
        result.push_back(static_cast<char>(c));
    }
    return result;
}

inline bool valid_evidence_origin(std::string_view value, std::size_t limit) {
    if (value.empty() || value.size() > limit ||
        !valid_utf8_metadata_text(value)) return false;
    for (const unsigned char c : value)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

inline bool is_structural_or_non_tag_field(const std::string& uppercase) {
    return is_protected_cue_metadata(uppercase) ||
           is_lyrics_metadata(uppercase) ||
           uppercase == "METADATA_BLOCK_PICTURE" ||
           uppercase.rfind("REPLAYGAIN_", 0) == 0;
}

// A field may appear more than once in foobar metadata. Never choose an
// arbitrary duplicate or flatten multi-value entries into a joined string.
inline std::vector<FieldReviewRow> review_online_fields(
    const MetadataDocument& original,
    const std::vector<FieldEvidence>& proposed_evidence) {
    std::vector<FieldReviewRow> rows;
    rows.reserve(proposed_evidence.size());
    // Same provider/source/scope cannot propose two independent values for
    // one physical tag. Detect both identical and conflicting duplicates;
    // different providers or recording-vs-edition scopes remain reviewable.
    using EvidenceKey = std::tuple<std::string, std::string, std::string, EvidenceScope>;
    std::map<EvidenceKey, std::size_t> occurrence_count;
    for (const auto& evidence : proposed_evidence)
        ++occurrence_count[{ascii_upper_field(evidence.field),
                            evidence.provider, evidence.source_id, evidence.scope}];
    for (std::size_t i = 0; i < proposed_evidence.size(); ++i) {
        const FieldEvidence& evidence = proposed_evidence[i];
        FieldReviewRow row;
        row.evidence_index = i;
        row.field = evidence.field;
        row.candidate = evidence;
        const auto name = ascii_upper_field(evidence.field);

        if (name.empty() || evidence.field.empty() ||
            evidence.field.size() > 256 ||
            !valid_utf8_metadata_text(evidence.field) ||
            !valid_evidence_origin(evidence.provider, 128) ||
            !valid_evidence_origin(evidence.source_id, 512)) {
            row.reason = "invalid_evidence_identity";
            rows.push_back(std::move(row));
            continue;
        }
        if (occurrence_count[{name, evidence.provider, evidence.source_id,
                              evidence.scope}] > 1) {
            row.reason = "duplicate_provider_field_evidence";
            rows.push_back(std::move(row));
            continue;
        }
        if (is_structural_or_non_tag_field(name)) {
            row.reason = "protected_metadata_field";
            rows.push_back(std::move(row));
            continue;
        }
        if (evidence.values.size() > 128) {
            row.reason = "unbounded_evidence_values";
            rows.push_back(std::move(row));
            continue;
        }
        if (evidence.values.empty()) {
            row.reason = "empty_evidence_must_not_delete_tag";
            rows.push_back(std::move(row));
            continue;
        }
        bool malformed_values = false;
        for (const auto& value : evidence.values) {
            if (value.empty() || value.size() > 8192 ||
                value.find('\0') != std::string::npos ||
                !valid_utf8_metadata_text(value)) {
                malformed_values = true;
                break;
            }
        }
        if (malformed_values) {
            row.reason = "invalid_or_empty_evidence_value";
            rows.push_back(std::move(row));
            continue;
        }
        if ((name == "DATE" || name == "DATE_RAW") &&
            evidence.date_meaning == DateMeaning::NotDate) {
            row.reason = "unknown_date_semantics";
            rows.push_back(std::move(row));
            continue;
        }

        std::size_t found_count = 0;
        for (std::size_t j = 0; j < original.fields.size(); ++j) {
            if (ascii_upper_field(original.fields[j].name) == name) {
                ++found_count;
                row.local_field_index = j;
                row.original_values = original.fields[j].values;
            }
        }
        if (found_count > 1) {
            row.local_field_index = no_local_field;
            row.original_values.clear();
            row.reason = "ambiguous_duplicate_local_field";
        } else if (found_count == 1 &&
                   row.original_values == evidence.values) {
            row.state = FieldReviewState::Unchanged;
            row.reason = "exact_noop";
        } else {
            row.state = FieldReviewState::NeedsReview;
            row.reason = found_count == 1
                ? "existing_values_differ" : "new_field_proposal";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace djmeta::online
