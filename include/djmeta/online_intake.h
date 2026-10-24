#pragma once

// Provider-independent, user-supplied text intake. Deliberately no network,
// shell, file, clipboard, SDK or writer access. Parsers return evidence ONLY.
#include "djmeta/online_fields.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online {

struct ManualCandidate {
    std::string provider;
    std::string source_id;
    EvidenceScope scope = EvidenceScope::Recording;
    // Optional explicit 0-based CUE order, never inferred from TRACK number
    // or FILE position. Allowed by the text intake for CUE comparison only.
    std::optional<std::size_t> cue_track_ordinal;
    std::vector<FieldEvidence> fields;
};

inline bool allowed_source_token(std::string_view text) {
    if (text.empty() || text.size() > 160) return false;
    for (const unsigned char ch : text) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == ':' || ch == '.' || ch == '/')) return false;
    }
    return true;
}
inline std::string_view strip_line_end(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}
inline void require_valid_tag_value(std::string_view value) {
    if (value.empty() || value.size() > 8192 ||
        value.find('\0') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos ||
        !valid_utf8_metadata_text(value))
        throw std::invalid_argument("invalid or empty manual candidate value");
}
inline DateMeaning parse_date_role(std::string_view value) {
    if (value == "original") return DateMeaning::OriginalRelease;
    if (value == "edition") return DateMeaning::EditionRelease;
    if (value == "digital") return DateMeaning::DigitalPublication;
    throw std::invalid_argument("unrecognized date role");
}
inline EvidenceScope parse_evidence_scope(std::string_view value) {
    if (value == "recording") return EvidenceScope::Recording;
    if (value == "release") return EvidenceScope::Release;
    if (value == "edition") return EvidenceScope::Edition;
    throw std::invalid_argument("unrecognized scope");
}

// Simple paste-compatible format (v1):
// @provider=discogs
// @id=release:100
// @scope=edition
// Optional for CUE recording-level comparison: @cue_track_ordinal=2
// (1-based absolute CUE order; never TRACK NN or subsong ID).
// TITLE=Title (Extended Mix)
// GENRE=House
// GENRE=Deep House
// DATE_RAW@edition=1998-05-06
//
// Repeated same-named lines are EXPLICIT multivalues, never split on ';'.
// Date meanings must be explicit. No deletions, no escapes, no inference.
// Fail closed on unknown directives, ambiguous roles, excess data or bad UTF-8.
inline ManualCandidate parse_manual_candidate(std::string_view raw) {
    if (raw.empty() || raw.size() > 65536)
        throw std::invalid_argument("manual candidate exceeds 64 KiB or is empty");
    if (!valid_utf8_metadata_text(raw))
        throw std::invalid_argument("invalid UTF-8 in manual candidate");
    ManualCandidate result;
    bool saw_provider = false, saw_id = false, saw_scope = false;
    bool saw_cue_track_ordinal = false;
    bool fields_started = false;
    std::size_t line_count = 0, offset = 0;
    while (offset < raw.size()) {
        if (++line_count > 160)
            throw std::invalid_argument("too many candidate lines");
        const auto next = raw.find('\n', offset);
        const auto line = strip_line_end(raw.substr(offset,
            next == std::string_view::npos ? raw.size() - offset : next - offset));
        offset = next == std::string_view::npos ? raw.size() : next + 1;
        if (line.empty()) continue;
        if (line.find('\r') != std::string_view::npos ||
            line.find('\0') != std::string_view::npos)
            throw std::invalid_argument("unexpected control character in line");
        const auto equal = line.find('=');
        if (equal == std::string_view::npos || equal == 0)
            throw std::invalid_argument("expected NAME=VALUE line");
        const auto raw_name = line.substr(0, equal);
        const auto raw_value = line.substr(equal + 1);
        if (raw_name.front() == '@') {
            if (fields_started)
                throw std::invalid_argument("source header after candidate fields");
            if (raw_name == "@provider") {
                if (saw_provider || !allowed_source_token(raw_value))
                    throw std::invalid_argument("invalid or duplicate provider");
                result.provider = std::string(raw_value);
                saw_provider = true;
            } else if (raw_name == "@id") {
                if (saw_id || !allowed_source_token(raw_value))
                    throw std::invalid_argument("invalid or duplicate source ID");
                result.source_id = std::string(raw_value);
                saw_id = true;
            } else if (raw_name == "@scope") {
                if (saw_scope) throw std::invalid_argument("duplicate scope");
                result.scope = parse_evidence_scope(raw_value);
                saw_scope = true;
            } else if (raw_name == "@cue_track_ordinal") {
                if (saw_cue_track_ordinal || raw_value.empty() ||
                    raw_value.front() == '0')
                    throw std::invalid_argument("invalid or duplicate CUE track ordinal");
                std::size_t ordinal = 0;
                const auto numeric = std::from_chars(
                    raw_value.data(), raw_value.data() + raw_value.size(), ordinal);
                if (numeric.ec != std::errc{} ||
                    numeric.ptr != raw_value.data() + raw_value.size() ||
                    ordinal < 1 || ordinal > 1024)
                    throw std::invalid_argument(
                        "CUE track ordinal must be a decimal number from 1 to 1024");
                result.cue_track_ordinal = ordinal - 1;
                saw_cue_track_ordinal = true;
            } else {
                throw std::invalid_argument("unknown manual candidate header");
            }
            continue;
        }
        fields_started = true;
        if (!saw_provider || !saw_id)
            throw std::invalid_argument("provider and source ID required first");
        auto field_part = raw_name;
        DateMeaning date_role = DateMeaning::NotDate;
        const auto at = raw_name.find('@');
        if (at != std::string_view::npos) {
            field_part = raw_name.substr(0, at);
            if (raw_name.find('@', at + 1) != std::string_view::npos)
                throw std::invalid_argument("multiple field role suffixes");
            date_role = parse_date_role(raw_name.substr(at + 1));
        }
        const auto upper = ascii_upper_field(field_part);
        if (upper.empty() || upper.size() > 120 || field_part.front() == '@' ||
            !valid_utf8_metadata_text(field_part) ||
            field_part.find('=') != std::string_view::npos)
            throw std::invalid_argument("invalid candidate field name");
        if (at != std::string_view::npos &&
            upper != "DATE" && upper != "DATE_RAW")
            throw std::invalid_argument("date role used on non-date field");
        if ((upper == "DATE" || upper == "DATE_RAW") &&
            date_role == DateMeaning::NotDate)
            throw std::invalid_argument("date role must be explicit");
        require_valid_tag_value(raw_value);
        std::size_t index = result.fields.size();
        for (std::size_t i = 0; i < result.fields.size(); ++i)
            if (ascii_upper_field(result.fields[i].field) == upper) {
                index = i;
                break;
            }
        if (index == result.fields.size()) {
            if (result.fields.size() >= 96)
                throw std::invalid_argument("too many candidate fields");
            FieldEvidence field;
            field.field = std::string(field_part);
            field.provider = result.provider;
            field.source_id = result.source_id;
            field.scope = result.scope;
            field.date_meaning = date_role;
            result.fields.push_back(std::move(field));
        } else if (result.fields[index].date_meaning != date_role) {
            throw std::invalid_argument("conflicting date meaning within field");
        }
        if (result.fields[index].values.size() >= 32)
            throw std::invalid_argument("too many candidate values per field");
        result.fields[index].values.emplace_back(raw_value);
    }
    if (!saw_provider || !saw_id || result.fields.empty())
        throw std::invalid_argument("manual candidate missing identity or fields");
    return result;
}

} // namespace djmeta::online
