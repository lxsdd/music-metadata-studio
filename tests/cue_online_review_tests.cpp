#include "djmeta/cue_online_review.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
void check(bool yes, const char* why) {
    if (!yes) { std::cerr << "FAIL: " << why << "\n"; std::exit(1); }
}
djmeta::online::FieldEvidence item(
    const char* field, std::vector<std::string> values,
    djmeta::online::EvidenceScope scope,
    djmeta::online::DateMeaning date = djmeta::online::DateMeaning::NotDate) {
    return {field, std::move(values), "discogs", "release:100", scope, date};
}
}

int main() {
    using namespace djmeta;
    using namespace djmeta::online;
    const std::string cue = "TITLE Album\nPERFORMER Album Artist\n"
        "REM GENRE House\nREM DATE 1998\n"
        "FILE x.flac WAVE\n"
        "TRACK 01 AUDIO\nTITLE First\nPERFORMER Soloist\n"
        "TRACK 02 AUDIO\nTITLE Second\n"
        "TRACK 03 AUDIO\nTITLE Third\n";
    const auto preserved = cue;
    const auto parsed = inspect_cue_metadata(cue, CueCarrierKind::EmbeddedText);
    check(parsed.status == CueSyntaxStatus::Parsed, "qualified source fixture");
    const auto rows = review_cue_candidate_fields(parsed, 1, {
        item("ALBUM", {"New Album"}, EvidenceScope::Edition),
        item("ALBUM ARTIST", {"Album Artist"}, EvidenceScope::Edition),
        item("GENRE", {"Techno"}, EvidenceScope::Edition),
        item("TITLE", {"Other Second"}, EvidenceScope::Recording),
        item("ARTIST", {"Album Artist"}, EvidenceScope::Recording),
        item("ARTIST", {"Other Singer"}, EvidenceScope::Recording),
        item("KEY", {"A minor"}, EvidenceScope::Recording),
        item("DATE", {"1997"}, EvidenceScope::Edition),
        item("GENRE", {"House", "Trance"}, EvidenceScope::Edition),
        item("CUESHEET", {"destructive"}, EvidenceScope::Recording),
        item("DATE", {"1998"}, EvidenceScope::Edition,
             DateMeaning::EditionRelease)
    });
    check(cue == preserved && rows.size() == 11,
          "review does not mutate raw CUE or truncate external evidence");
    check(rows[0].cue_field == "TITLE" &&
          rows[0].target == CueReviewTarget::AlbumGlobal &&
          rows[0].state == FieldReviewState::NeedsReview &&
          rows[0].affected_inheriting_tracks == 0,
          "ALBUM -> global TITLE, never inherited as a per-track title");
    check(rows[1].cue_field == "PERFORMER" &&
          rows[1].state == FieldReviewState::Unchanged &&
          rows[1].affected_inheriting_tracks == 0,
          "existing global PERFORMER exact no-op and zero write intent");
    check(rows[2].cue_field == "REM GENRE" &&
          rows[2].affected_inheriting_tracks == 3 &&
          rows[2].state == FieldReviewState::NeedsReview,
          "album genre proposal previews impact on all inheriting tracks");
    check(rows[3].target == CueReviewTarget::TrackLocal &&
          rows[3].track_ordinal == 1 &&
          rows[3].cue_field == "TITLE" &&
          rows[3].original_values == std::vector<std::string>({"Second"}) &&
          rows[3].state == FieldReviewState::NeedsReview,
          "selected track TITLE is not album TITLE");
    check(rows[4].state == FieldReviewState::Unchanged &&
          rows[4].original_inherited,
          "track inherits identical global artist -> no-op");
    check(rows[5].state == FieldReviewState::NeedsReview &&
          rows[5].original_inherited &&
          rows[5].reason == "track_override_would_be_required",
          "different track artist must propose override, not change global performer");
    check(rows[6].state == FieldReviewState::Blocked &&
          rows[6].reason == "cue_field_not_representable",
          "KEY, LABEL, BPM and unqualified custom REM fields never invented");
    check(rows[7].state == FieldReviewState::Blocked &&
          rows[7].reason == "cue_date_role_unverified",
          "date without semantic source meaning cannot silently overwrite REM DATE");
    check(rows[8].state == FieldReviewState::Blocked &&
          rows[8].reason == "cue_evidence_invalid_or_multivalue",
          "CUE does not gain unsupported multi-value genres");
    check(rows[9].state == FieldReviewState::Blocked &&
          rows[9].reason == "cue_field_not_representable",
          "a provider cannot rewrite the protected CUESHEET structure");
    check(rows[10].state == FieldReviewState::Unchanged &&
          rows[10].cue_field == "REM DATE",
          "identical qualified release date is a no-op");

    const auto missing_track = review_cue_candidate_fields(parsed, std::nullopt, {
        item("TITLE", {"Song"}, EvidenceScope::Recording)
    });
    check(missing_track[0].state == FieldReviewState::Blocked &&
          missing_track[0].reason == "cue_track_identity_missing",
          "no track fallback from album or arbitrary first CUE track");

    const auto duplicate = inspect_cue_metadata(
        "REM GENRE House\nREM GENRE Techno\n"
        "FILE x.flac WAVE\nTRACK 01 AUDIO\nTITLE Song\nPERFORMER Artist\n",
        CueCarrierKind::ExternalText);
    const auto conflicts = review_cue_candidate_fields(duplicate, 0, {
        item("GENRE", {"House"}, EvidenceScope::Release)
    });
    check(conflicts[0].state == FieldReviewState::Blocked &&
          conflicts[0].reason == "cue_duplicated_target_token" &&
          conflicts[0].original_values.size() == 2,
          "ambiguous repeated global raw tokens remain preserved, never flattened");

    const auto malformed = inspect_cue_metadata(
        "FILE x.flac WAVE\nTRACK 01 AUDIO\nTITLE \"broken\n",
        CueCarrierKind::ExternalText);
    const auto rejected = review_cue_candidate_fields(malformed, 0, {
        item("TITLE", {"Some Song"}, EvidenceScope::Recording)
    });
    check(rejected[0].state == FieldReviewState::Blocked &&
          rejected[0].reason == "cue_inventory_unqualified",
          "no proposals from unqualified syntax or encoding");

    std::cout << "PASS: CUE field scope, inheritance impact, no-op, unknown-field guards\n";
}
