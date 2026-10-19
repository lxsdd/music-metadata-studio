#include "djmeta/cue_release_review.h"

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
    djmeta::online::DateMeaning date = djmeta::online::DateMeaning::NotDate,
    const char* source_id = "release:100") {
    return {field, std::move(values), "discogs", source_id, scope, date};
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
        item("ARTIST", {"Other Singer"}, EvidenceScope::Recording,
             DateMeaning::NotDate, "recording:200"),
        item("KEY", {"A minor"}, EvidenceScope::Recording),
        item("DATE", {"1997"}, EvidenceScope::Edition,
             DateMeaning::NotDate, "release:101"),
        item("GENRE", {"House", "Trance"}, EvidenceScope::Edition,
             DateMeaning::NotDate, "release:102"),
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


    // Provider strings are untrusted; CR/LF must not inject CUE structure.
    const auto invalid_rows = review_cue_candidate_fields(parsed, 1, {
        item("TITLE", {"Safe\nFILE stolen.flac WAVE"}, EvidenceScope::Recording),
        {"TITLE", {"Value"}, "discogs\nspoof", "release:100",
         EvidenceScope::Recording, DateMeaning::NotDate},
        {"TITLE", {"Value"}, "discogs", "release:100\rspoof",
         EvidenceScope::Recording, DateMeaning::NotDate},
        item("TITLE", {std::string(8193, 'x')}, EvidenceScope::Recording),
        item(std::string(257, 'F').c_str(), {"Value"}, EvidenceScope::Recording)
    });
    check(invalid_rows.size() == 5, "invalid evidence is visible for review");
    for (const auto& row : invalid_rows)
        check(row.state == FieldReviewState::Blocked &&
              row.reason == "cue_evidence_invalid_or_multivalue",
              "malformed provenance and CUE line injection fail closed");

    const auto repeated = review_cue_candidate_fields(parsed, 1, {
        item("TITLE", {"Other Second"}, EvidenceScope::Recording),
        item("title", {"Second"}, EvidenceScope::Recording)
    });
    check(repeated.size() == 2 &&
          repeated[0].reason == "cue_duplicate_provider_field_evidence" &&
          repeated[1].reason == "cue_duplicate_provider_field_evidence" &&
          repeated[0].state == FieldReviewState::Blocked &&
          repeated[1].state == FieldReviewState::Blocked,
          "same-source case aliases cannot compete for one CUE token");

    const auto independent = review_cue_candidate_fields(parsed, 1, {
        item("TITLE", {"Other Second"}, EvidenceScope::Recording),
        item("TITLE", {"Second"}, EvidenceScope::Recording,
             DateMeaning::NotDate, "recording:200")
    });
    check(independent[0].state == FieldReviewState::NeedsReview &&
          independent[1].state == FieldReviewState::Unchanged,
          "distinct recording sources remain independently reviewable");

    const auto edition_date = review_cue_candidate_fields(parsed, 1, {
        item("DATE", {"1997"}, EvidenceScope::Edition,
             DateMeaning::OriginalRelease, "release:201"),
        item("DATE", {"1997"}, EvidenceScope::Edition,
             DateMeaning::DigitalPublication, "release:202"),
        item("DATE", {"1998"}, EvidenceScope::Edition,
             DateMeaning::EditionRelease, "release:203")
    });
    check(edition_date[0].reason == "cue_date_scope_mismatch" &&
          edition_date[1].reason == "cue_date_scope_mismatch" &&
          edition_date[2].state == FieldReviewState::Unchanged,
          "original/digital dates cannot overwrite edition CUE date");


    // Edition-scoped Discogs track credits must remain track-local in CUE,
    // while the same release's album fields stay global. Source provenance
    // is never rewritten to Recording just to obtain a CUE target.
    EditionTrack edition_track;
    edition_track.source_track_id = "A2";
    edition_track.fields = {
        item("TITLE", {"Second (Extended Mix)"}, EvidenceScope::Edition,
             DateMeaning::NotDate, "881/track/2"),
        item("SONGWRITER", {"Writer A"}, EvidenceScope::Edition,
             DateMeaning::NotDate, "881/track/2"),
        item("REMIXER", {"DJ One"}, EvidenceScope::Edition,
             DateMeaning::NotDate, "881/track/2"),
        item("DATE", {"2026"}, EvidenceScope::Edition,
             DateMeaning::EditionRelease, "881/track/2")
    };
    const auto edition_track_rows = review_cue_edition_track_fields(
        parsed, 1, edition_track);
    check(edition_track_rows.size() == 4 &&
          edition_track_rows[0].target == CueReviewTarget::TrackLocal &&
          edition_track_rows[0].cue_field == "TITLE" &&
          edition_track_rows[0].track_ordinal == 1 &&
          edition_track_rows[0].original_values ==
              std::vector<std::string>({"Second"}) &&
          edition_track_rows[0].candidate.scope == EvidenceScope::Edition &&
          edition_track_rows[0].candidate.source_id == "881/track/2" &&
          edition_track_rows[1].cue_field == "SONGWRITER" &&
          edition_track_rows[1].state == FieldReviewState::NeedsReview &&
          edition_track_rows[2].state == FieldReviewState::Blocked &&
          edition_track_rows[2].reason == "cue_field_not_representable" &&
          edition_track_rows[3].state == FieldReviewState::Blocked &&
          edition_track_rows[3].reason == "cue_edition_date_not_track_local",
          "edition track credits target only the selected CUE track with intact provenance");
    const auto missing_edition_track = review_cue_edition_track_fields(
        parsed, std::nullopt, edition_track);
    check(missing_edition_track[0].state == FieldReviewState::Blocked &&
          missing_edition_track[0].reason == "cue_track_identity_missing",
          "no CUE track association means no edition-track proposal");
    auto mixed_edition_track = edition_track;
    mixed_edition_track.fields[1].source_id = "881/track/3";
    const auto mixed_edition_rows = review_cue_edition_track_fields(
        parsed, 1, mixed_edition_track);
    for (const auto& row : mixed_edition_rows)
        check(row.state == FieldReviewState::Blocked &&
              row.reason == "cue_edition_track_identity_unqualified",
              "mixed edition-track source identities fail closed");

    // Exact Discogs edition -> immutable CUE inventory -> qualified track
    // comparison. The unmatched second track must never borrow first-track
    // credits, even though title and artist happen to agree.
    const std::string release_cue =
        "TITLE Release\nPERFORMER \"Various Artists\"\n"
        "FILE \"mix.flac\" WAVE\n"
        "TRACK 01 AUDIO\nTITLE \"Solar (Extended Mix)\"\n"
        "PERFORMER \"Artist A\"\nISRC GBABC2600001\n"
        "INDEX 01 00:00:00\n"
        "TRACK 02 AUDIO\nTITLE Lunar\nPERFORMER \"Artist B\"\n"
        "INDEX 01 04:00:00\n";
    const auto release_inventory = inspect_cue_metadata(
        release_cue, CueCarrierKind::ExternalText);
    ReleaseEdition edition{
        "discogs", "881",
        {item("ALBUM", {"Release"}, EvidenceScope::Edition,
              DateMeaning::NotDate, "881")},
        {
            {"2", {"Lunar", "Artist B", "", MixKind::Unknown, "", -1},
             -1, 2, {item("TITLE", {"Lunar"}, EvidenceScope::Edition,
                          DateMeaning::NotDate, "881/track/1")}},
            {"1", {"Solar (Extended Mix)", "Artist A", "",
                    MixKind::Unknown, "GBABC2600001", -1},
             -1, 1, {
                 item("TITLE", {"Solar (Extended Mix)"}, EvidenceScope::Edition,
                      DateMeaning::NotDate, "881/track/2"),
                 item("SONGWRITER", {"Writer A"}, EvidenceScope::Edition,
                      DateMeaning::NotDate, "881/track/2")
             }}
        }
    };
    const auto reviewed = review_cue_release_edition(release_inventory, edition);
    check(reviewed.eligible && reviewed.album_fields.size() == 1 &&
          reviewed.album_fields[0].state == FieldReviewState::Unchanged &&
          reviewed.tracks.size() == 2 &&
          reviewed.tracks[0].edition_track_index == 1 &&
          reviewed.tracks[0].decision == MatchDecision::Suggested &&
          reviewed.tracks[0].fields.size() == 2 &&
          reviewed.tracks[0].fields[0].state == FieldReviewState::Unchanged &&
          reviewed.tracks[0].fields[1].cue_field == "SONGWRITER" &&
          reviewed.tracks[0].fields[1].state == FieldReviewState::NeedsReview,
          "exact Discogs edition credits map to one CUE ordinal");
    check(reviewed.tracks[1].decision == MatchDecision::Review &&
          reviewed.tracks[1].fields.empty() &&
          !reviewed.tracks[1].reasons.empty(),
          "uncertain CUE match cannot receive edition track credits");

    auto foreign_album = edition;
    foreign_album.fields[0].source_id = "another-edition";
    const auto refused_album = review_cue_release_edition(
        release_inventory, foreign_album);
    check(!refused_album.eligible && refused_album.album_fields.empty() &&
          refused_album.tracks.empty(),
          "foreign edition album provenance blocks all comparisons");

    auto foreign_track = edition;
    foreign_track.tracks[1].fields[1].provider = "musicbrainz";
    const auto refused_track = review_cue_release_edition(
        release_inventory, foreign_track);
    check(refused_track.eligible && refused_track.tracks[0].fields.empty() &&
          !refused_track.tracks[0].reasons.empty(),
          "mixed provider track credits cannot enter CUE comparison");
    const auto direct_mixed = review_cue_edition_track_fields(
        release_inventory, 0, foreign_track.tracks[1]);
    check(direct_mixed.size() == 2 &&
          direct_mixed[0].state == FieldReviewState::Blocked &&
          direct_mixed[1].state == FieldReviewState::Blocked,
          "direct track review also blocks mixed provider provenance");

    auto forged = edition;
    forged.tracks[1].fields[0].source_id = "881/track/999";
    forged.tracks[1].fields[1].source_id = "881/track/999";
    const auto refused_id = review_cue_release_edition(
        release_inventory, forged);
    check(refused_id.tracks[0].fields.empty() &&
          !refused_id.tracks[0].reasons.empty(),
          "Discogs track provenance must match selected edition row");

    const auto broken_cue = inspect_cue_metadata(
        "FILE x.flac WAVE\nTRACK 01 AUDIO\nTITLE \"broken\n",
        CueCarrierKind::ExternalText);
    check(!review_cue_release_edition(broken_cue, edition).eligible,
          "invalid CUE cannot enter edition comparison");
    std::cout << "PASS: CUE field scope, inheritance impact, no-op, unknown-field guards\n";
}
