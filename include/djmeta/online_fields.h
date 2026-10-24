#pragma once

// Read-only UI-facing comparison of local metadata vectors and provider
// evidence. No automatic source selection, deletion, or write authorization.
#include "djmeta/normalizer.h"
#include "djmeta/online_release.h"
#include "djmeta/structural_guard.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <stdexcept>
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

// A provider-absent original is a physical tag, not a remote response.
// Keep carrier provenance distinct from edition or recording evidence.
inline bool is_local_physical_original(const FieldReviewRow& row) {
    if(row.candidate.provider!="local-physical") return false;
    return row.reason=="discogs_local_only_no_provider_value" ||
           row.reason=="musicbrainz_local_only_no_provider_value" ||
           row.reason=="manual_beatport_local_only_no_provider_value" ||
           row.reason=="local_physical_inventory_readonly" ||
           row.reason=="local_physical_inventory_overflow" ||
           row.reason=="local_physical_group";
}
inline FieldReviewRow make_local_physical_original(
    const djmeta::MetadataField& source,std::string reason) {
    FieldReviewRow row;
    row.field=source.name;
    row.original_values=source.values;
    row.candidate.field=source.name;
    // Logical physical snapshot carrier; not a remote catalogue identity.
    row.candidate.provider="local-physical";
    row.candidate.source_id="physical-original";
    row.candidate.scope=EvidenceScope::Recording;
    row.state=FieldReviewState::Unchanged;
    row.reason=std::move(reason);
    return row;
}

// A search-result detail pane must never silently drop physical tags at
// the old 50-row limit. Bound per-hit allocations to the native pane's
// capacity; explicitly disclose any remaining fields and point to the
// separate full "All current tags" inventory. Original vectors stay intact.
inline void append_local_physical_inventory(
    std::vector<FieldReviewRow>& detail,const djmeta::MetadataDocument& source,
    std::size_t maximum_visible=1024) {
    if(maximum_visible<2 || detail.size()>=maximum_visible)
        throw std::invalid_argument("Local source inventory cannot be displayed");
    const auto remaining_capacity=maximum_visible-detail.size();
    const bool overflow=source.fields.size()>remaining_capacity;
    const std::size_t count=overflow
        ? remaining_capacity-1:source.fields.size();
    for(std::size_t i=0;i<count;++i)
        detail.push_back(make_local_physical_original(
            source.fields[i],"local_physical_inventory_readonly"));
    if(overflow) {
        const auto hidden=source.fields.size()-count;
        const djmeta::MetadataField notice{
            "Additional physical tags not displayed",
            {std::to_string(hidden)+
             " original field(s) remain. Review proposals > All current tags."}};
        auto row=make_local_physical_original(
            notice,"local_physical_inventory_overflow");
        row.state=FieldReviewState::Blocked;
        detail.push_back(std::move(row));
    }
}

// The UI must not label pasted or unverified evidence as an official HTTPS
// response merely because a user supplied the provider name. The origin is
// determined by both the production-set reason and the provider identity.
inline std::string online_evidence_origin_label(const FieldReviewRow& row) {
    if(is_local_physical_original(row)) return "Local physical";
    if (row.reason == "cue_inventory_read_only" ||
        row.reason == "cue_inventory_unqualified")
        return "Local CUE";
    if (row.candidate.provider == "beatport" &&
        row.reason.starts_with("beatport_public_webpage_"))
        return "Beatport public page (unverified)";
    if (row.candidate.provider == "discogs" &&
        row.reason.starts_with("discogs_live_"))
        return "Discogs API";
    if (row.candidate.provider == "musicbrainz" &&
        row.reason.starts_with("musicbrainz_live_"))
        return "MusicBrainz API";
    return "Pasted: " + row.candidate.provider;
}

inline constexpr std::string_view online_evidence_scope_label(EvidenceScope scope) {
    switch (scope) {
        case EvidenceScope::Recording: return "recording";
        case EvidenceScope::Release: return "release";
        case EvidenceScope::Edition: return "exact edition";
    }
    return "unknown";
}

inline constexpr std::string_view online_evidence_date_label(DateMeaning meaning) {
    switch (meaning) {
        case DateMeaning::NotDate: return "not a date";
        case DateMeaning::OriginalRelease: return "original release";
        case DateMeaning::EditionRelease: return "edition release";
        case DateMeaning::DigitalPublication: return "digital publication";
    }
    return "unknown";
}

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

// One canonical date-field vocabulary for clipboard intake and field review.
// Providers must supply an explicit original/edition/digital meaning for each
// of these tags; otherwise the comparison fails closed before any writer.
inline constexpr bool is_online_date_field(std::string_view uppercase) noexcept {
    return uppercase=="DATE" || uppercase=="DATE_RAW" ||
           uppercase=="ORIGINALDATE" || uppercase=="RELEASEDATE" ||
           uppercase=="PUBLISHDATE" || uppercase=="PUBLISH_DATE";
}

inline bool is_structural_or_non_tag_field(const std::string& uppercase) {
    return is_protected_cue_metadata(uppercase) ||
           is_lyrics_metadata(uppercase) ||
           uppercase == "MATCH_DIAGNOSTIC" ||
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
        // Every physical date-like tag must carry a known temporal meaning.
        // A provider cannot disguise an untyped RELEASEDATE as a safe value,
        // nor smuggle a claimed original date into a non-date target.
        if (is_online_date_field(name) !=
            (evidence.date_meaning!=DateMeaning::NotDate)) {
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
