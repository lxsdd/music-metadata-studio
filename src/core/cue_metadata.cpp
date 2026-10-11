#include "djmeta/cue_metadata.h"
#include "djmeta/structural_guard.h"

#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace djmeta {
namespace {

constexpr std::size_t none = (std::numeric_limits<std::size_t>::max)();
bool blank(char ch) { return ch == ' ' || ch == '\t'; }
char uppercase(char ch) {
    return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
}
std::string upper(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) result.push_back(uppercase(ch));
    return result;
}
std::string_view next_word(std::string_view line, std::size_t& offset) {
    while (offset < line.size() && blank(line[offset])) ++offset;
    const auto first = offset;
    while (offset < line.size() && !blank(line[offset])) ++offset;
    return line.substr(first, offset - first);
}
bool recognized(std::string_view verb) {
    return verb == "TITLE" || verb == "PERFORMER" ||
           verb == "SONGWRITER" || verb == "ISRC" ||
           verb == "REM GENRE" || verb == "REM DATE" ||
           verb == "REM COMMENT" || verb == "REM DISCNUMBER" ||
           verb == "REM TOTALDISCS";
}
struct ParsedValue {
    bool valid = false;
    bool quoted = false;
    std::size_t begin = 0;
    std::size_t end = 0;
};
ParsedValue extract_value(std::string_view line, std::size_t offset) {
    while (offset < line.size() && blank(line[offset])) ++offset;
    if (offset == line.size()) return {};
    ParsedValue result;
    result.quoted = line[offset] == '"';
    if (result.quoted) {
        result.begin = offset + 1;
        result.end = result.begin;
        while (result.end < line.size() && line[result.end] != '"') ++result.end;
        if (result.end == line.size()) return {};
        auto after = result.end + 1;
        while (after < line.size() && blank(line[after])) ++after;
        if (after != line.size()) return {};
    } else {
        result.begin = offset;
        result.end = line.size();
        while (result.end > result.begin && blank(line[result.end - 1]))
            --result.end;
    }
    const auto value = line.substr(result.begin, result.end - result.begin);
    if (value.empty() || value.find('\0') != std::string_view::npos ||
        !valid_utf8_metadata_text(value)) return {};
    result.valid = true;
    return result;
}
void review(CueMetadataInventory& out, const char* diagnostic) {
    if (out.status == CueSyntaxStatus::Parsed) out.status = CueSyntaxStatus::NeedsReview;
    out.diagnostics.emplace_back(diagnostic);
}
bool decimal_track(std::string_view value, int& track) {
    if (value.empty() || value.size() > 3) return false;
    int n = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    if (n <= 0 || n > 999) return false;
    track = n;
    return true;
}
bool is_field_for_scope(std::string_view name, bool track) {
    if (!recognized(name)) return false;
    if (!track && name == "ISRC") return false;
    return true;
}

} // namespace

CueMetadataInventory inspect_cue_metadata(
    std::string_view source, CueCarrierKind carrier) {
    CueMetadataInventory out;
    out.carrier = carrier;
    out.source_bytes = source.size();
    const auto files = inspect_external_cue(source);
    out.status = files.status;
    out.encoding = files.encoding;
    out.files = files.references;
    out.diagnostics = files.diagnostics;

    // The shared FILE scanner refuses unknown text encodings, malformed FILEs
    // and oversized data. Don't turn any such document into a confident
    // metadata inventory.
    if (files.status == CueSyntaxStatus::Invalid ||
        files.encoding == CueTextEncoding::Unknown ||
        files.encoding == CueTextEncoding::UnsupportedUtf16 ||
        source.size() > 8u * 1024u * 1024u) return out;

    std::set<std::pair<std::size_t, int>> track_numbers_per_file;
    // Match the FILE inventory's initial UTF-8 BOM handling: the BOM is
    // transport metadata, not part of the first CUE command or raw value.
    const std::size_t initial_offset =
        out.encoding == CueTextEncoding::Utf8Bom ? 3u : 0u;
    std::size_t line_number = 0, offset = initial_offset, file_count = 0;
    std::size_t current_track = none;
    while (offset < source.size()) {
        if (++line_number > 100000) {
            review(out, "CUE_LINE_LIMIT_EXCEEDED");
            return out;
        }
        const auto eol = source.find_first_of("\r\n", offset);
        const auto end = eol == std::string_view::npos ? source.size() : eol;
        const auto line = source.substr(offset, end - offset);
        std::size_t pos = 0;
        const auto command = upper(next_word(line, pos));
        if (command == "FILE") {
            ++file_count;
            current_track = none;
        } else if (command == "TRACK") {
            const auto number_text = next_word(line, pos);
            const auto type_text = next_word(line, pos);
            const auto trailing = next_word(line, pos);
            int number = 0;
            if (!decimal_track(number_text, number) || type_text.empty() ||
                !trailing.empty() || file_count == 0 || out.tracks.size() >= 1024) {
                review(out, "CUE_TRACK_DECLARATION_UNQUALIFIED");
                current_track = none;
            } else {
                CueTrackMetadata track;
                track.ordinal = out.tracks.size();
                track.declared_track_number = number;
                track.track_type = upper(type_text);
                track.file_reference_index = file_count - 1;
                track.line_number = line_number;
                if (!track_numbers_per_file.emplace(track.file_reference_index, number).second)
                    review(out, "CUE_DUPLICATE_TRACK_NUMBER_WITHIN_FILE");
                if (track.track_type != "AUDIO")
                    review(out, "CUE_NON_AUDIO_TRACK");
                current_track = out.tracks.size();
                out.tracks.push_back(std::move(track));
            }
        } else {
            auto name = command;
            if (command == "REM") {
                const auto rem_type = upper(next_word(line, pos));
                name += " " + rem_type;
            }
            if (is_field_for_scope(name, current_track != none)) {
                const auto value = extract_value(line, pos);
                if (!value.valid) {
                    review(out, "CUE_METADATA_VALUE_UNQUALIFIED");
                } else {
                    CueMetadataField field;
                    field.name = std::move(name);
                    field.value = std::string(line.substr(value.begin, value.end - value.begin));
                    field.scope = current_track == none
                        ? CueFieldScope::AlbumGlobal : CueFieldScope::TrackLocal;
                    field.line_number = line_number;
                    field.value_begin = offset + value.begin;
                    field.value_end = offset + value.end;
                    field.quoted = value.quoted;
                    if (current_track == none) out.globals.push_back(std::move(field));
                    else out.tracks[current_track].local_fields.push_back(std::move(field));
                }
            } else if (command == "ISRC" && current_track == none) {
                review(out, "CUE_ISRC_OUTSIDE_TRACK");
            }
        }
        if (eol == std::string_view::npos) break;
        offset = eol + 1;
        if (source[eol] == '\r' && offset < source.size() && source[offset] == '\n')
            ++offset;
    }
    if (file_count != out.files.size()) review(out, "CUE_FILE_INVENTORY_MISMATCH");
    if (out.tracks.empty()) {
        out.status = CueSyntaxStatus::Invalid;
        out.diagnostics.emplace_back("CUE_NO_TRACK_DECLARATIONS");
    }
    return out;
}

CueEffectiveField effective_cue_field(
    const CueMetadataInventory& inventory,
    std::size_t ordinal,
    std::string_view cue_field_name) {
    CueEffectiveField result;
    if (ordinal >= inventory.tracks.size() ||
        inventory.status == CueSyntaxStatus::Invalid) return result;
    const auto name = upper(cue_field_name);
    if (!recognized(name)) return result;
    const CueMetadataField* selected = nullptr;
    std::size_t count = 0;
    for (const auto& field : inventory.tracks[ordinal].local_fields)
        if (field.name == name) {
            ++count;
            selected = &field;
        }
    if (count > 1) { result.ambiguous = true; return result; }
    if (count == 1) {
        result.present = true;
        result.value = selected->value;
        result.source_line_number = selected->line_number;
        return result;
    }
    // A global CUE TITLE is the ALBUM title, never a virtual track TITLE.
    // ISRC belongs only to a track. The other supported document fields
    // may be inherited by virtual tracks; never infer a new physical tag.
    if (name == "TITLE" || name == "ISRC") return result;
    for (const auto& field : inventory.globals)
        if (field.name == name) {
            ++count;
            selected = &field;
        }
    if (count > 1) { result.ambiguous = true; return result; }
    if (count == 1) {
        result.present = true;
        result.inherited = true;
        result.value = selected->value;
        result.source_line_number = selected->line_number;
    }
    return result;
}

} // namespace djmeta
