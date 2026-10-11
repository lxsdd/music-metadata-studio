#include "djmeta/online_fields.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void check(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace djmeta;
    using namespace djmeta::online;
    const MetadataDocument original{{
        {"ARTIST", {"A", "B"}},
        {"DATE", {"1998"}},
        {"GENRE", {"House"}},
        {"COMMENT", {"keep this"}},
        {"CUESHEET", {"FILE a.mp3 MP3\nTRACK 01 AUDIO"}},
        {"X_CUSTOM", {"leave alone"}}
    }};
    const auto saved = original;
    const std::vector<FieldEvidence> evidence{
        {"artist", {"A", "B"}, "discogs", "release:100"},
        {"GENRE", {"Tech House"}, "beatport", "track:500"},
        {"DATE", {"2005-04-01"}, "discogs", "release:100",
          EvidenceScope::Edition, DateMeaning::EditionRelease},
        {"DATE_RAW", {"1998-06-15"}, "musicbrainz", "recording:1",
          EvidenceScope::Recording, DateMeaning::OriginalRelease},
        {"COMMENT", {}, "discogs", "release:100"},
        {"CUESHEET", {"edited"}, "discogs", "release:100"},
        {"REPLAYGAIN_TRACK_GAIN", {"+0.3 dB"}, "discogs", "release:100"},
        {"X_CUSTOM", {"should remain"}, "", ""},
        {"DATE", {"1970"}, "discogs", "release:100"},
        {"ALBUM", {"Collection", "Special Edition"}, "discogs", "release:100",
          EvidenceScope::Edition, DateMeaning::NotDate}
    };
    const auto rows = review_online_fields(original, evidence);
    check(original == saved && rows.size() == evidence.size(),
          "immutable input and exactly one review row per source evidence");
    check(rows[0].state == FieldReviewState::Unchanged &&
          rows[0].reason == "exact_noop" &&
          rows[0].original_values == std::vector<std::string>({"A", "B"}) &&
          rows[0].local_field_index == 0,
          "multivalue artist unchanged without writer authorization");
    check(rows[1].state == FieldReviewState::NeedsReview &&
          rows[1].reason == "existing_values_differ",
          "changed genre is a REVIEW, not an automated tag write");
    check(rows[2].state == FieldReviewState::NeedsReview &&
          rows[2].candidate.date_meaning == DateMeaning::EditionRelease &&
          rows[2].original_values == std::vector<std::string>({"1998"}),
          "edition date must retain DATE provenance and original");
    check(rows[3].state == FieldReviewState::NeedsReview &&
          rows[3].local_field_index == no_local_field &&
          rows[3].candidate.date_meaning == DateMeaning::OriginalRelease,
          "DATE_RAW can be proposed but never silently derived from DATE");
    check(rows[4].state == FieldReviewState::Blocked &&
          rows[4].reason == "empty_evidence_must_not_delete_tag",
          "empty provider field cannot delete an existing tag");
    check(rows[5].state == FieldReviewState::Blocked &&
          rows[5].reason == "protected_metadata_field" &&
          rows[6].state == FieldReviewState::Blocked,
          "CUE and ReplayGain remain under dedicated writer boundaries");
    check(rows[7].state == FieldReviewState::Blocked &&
          rows[7].reason == "invalid_evidence_identity",
          "missing provider identity is not an authoritative source");
    check(rows[8].state == FieldReviewState::Blocked &&
          rows[8].reason == "unknown_date_semantics",
          "date of unknown meaning cannot silently replace DATE");
    check(rows[9].state == FieldReviewState::NeedsReview &&
          rows[9].candidate.values.size() == 2,
          "unknown future custom fields and multivalue inputs remain visible");

    const MetadataDocument duplicate{{{"TITLE", {"a"}}, {"title", {"b"}}}};
    const auto duplicates = review_online_fields(
        duplicate, {{"title", {"c"}, "deezer", "track:3"}});
    check(duplicates.size() == 1 &&
          duplicates[0].state == FieldReviewState::Blocked &&
          duplicates[0].local_field_index == no_local_field &&
          duplicates[0].reason == "ambiguous_duplicate_local_field",
          "duplicate names cannot be flattened or arbitrarily selected");
    const auto malformed = review_online_fields(
        original, {{std::string("ARTIST\0", 7), {"X"}, "deezer", "track:1"}});
    check(malformed[0].state == FieldReviewState::Blocked &&
          malformed[0].reason == "invalid_evidence_identity",
          "invalid control byte in tag name cannot enter draft");

    const auto extra_safety = review_online_fields(original, {
        {"USLT", {"words"}, "discogs", "r1"},
        {"SYLT", {"words"}, "discogs", "r1"},
        {"COMMENT", {std::string("A\0B", 3)}, "discogs", "r1"},
        {"TITLE", {std::string("\xC0\xAF", 2)}, "discogs", "r1"},
        {"GENRE", {""}, "discogs", "r1"}
    });
    check(extra_safety.size() == 5 &&
          extra_safety[0].reason == "protected_metadata_field" &&
          extra_safety[1].reason == "protected_metadata_field" &&
          extra_safety[2].reason == "invalid_or_empty_evidence_value" &&
          extra_safety[3].reason == "invalid_or_empty_evidence_value" &&
          extra_safety[4].reason == "invalid_or_empty_evidence_value",
          "shared lyrics safety and UTF-8/NUL/empty provider-value guards");

    std::cout << "PASS: online field provenance, protected tags, no-op and review-only\n";
}
