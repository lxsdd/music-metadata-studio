#pragma once
// Deliberate Discogs discovery hints only; no tag value or provider response
// is elevated into a verified recording/release association by these helpers.
#include "djmeta/discogs_release_review.h"
#include "djmeta/cue_metadata.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace djmeta::online::discogs {

struct LocalSearchHints {
    std::string title;
    std::string artist;
    std::string album;
    std::string barcode;
    std::string catalog_number;
};

// Optional source hints must never veto an explicitly typed search or
// canonical release URL. Duplicate, oversized, invalid or multivalue fields
// are simply omitted; original tags remain independently observable.
inline std::string unique_optional_physical_hint(
    const MetadataDocument& source, std::string_view name) {
    const auto value=unique_discogs_physical_tag(source,name);
    if(!value) return {};
    try { return validated_term(*value); }
    catch(const std::invalid_argument&) { return {}; }
}

inline LocalSearchHints physical_search_hints(
    const MetadataDocument& source) {
    LocalSearchHints hints;
    hints.title=unique_optional_physical_hint(source,"TITLE");
    hints.artist=unique_optional_physical_hint(source,"ARTIST");
    hints.album=unique_optional_physical_hint(source,"ALBUM");
    const auto barcode=unique_optional_physical_hint(source,"BARCODE");
    const auto catalog=unique_optional_physical_hint(source,"CATALOGNUMBER");
    if(catalog_label(barcode,150)) hints.barcode=barcode;
    if(catalog_label(catalog,150)) hints.catalog_number=catalog;
    return hints;
}

inline std::string unique_optional_cue_global_hint(
    const CueMetadataInventory& cue, std::string_view name) {
    if(cue.status!=CueSyntaxStatus::Parsed) return {};
    const CueMetadataField* found=nullptr;
    for(const auto& field:cue.globals) {
        if(field.name!=name) continue;
        if(found) return {}; // Keep duplicates, never choose arbitrarily.
        found=&field;
    }
    if(!found) return {};
    try { return validated_term(found->value); }
    catch(const std::invalid_argument&) { return {}; }
}
inline LocalSearchHints cue_search_hints(const CueMetadataInventory& cue) {
    LocalSearchHints hints;
    hints.title=unique_optional_cue_global_hint(cue,"TITLE");
    hints.album=hints.title; // CUE global TITLE is ALBUM, not a track TITLE.
    hints.artist=unique_optional_cue_global_hint(cue,"PERFORMER");
    // A textual CUE provides no trusted barcode/catno from virtual tags.
    return hints;
}

} // namespace djmeta::online::discogs
