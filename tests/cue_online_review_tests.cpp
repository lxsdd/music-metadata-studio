#include "djmeta/cue_release_review.h"
#include "djmeta/cue_original_inventory.h"
#include "djmeta/cue_reference_locator.h"
#include "djmeta/native_candidate_labels.h"
#include "djmeta/discogs_provider.h"
#include "djmeta/discogs_search_hints.h"

#include <algorithm>
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
    // Studio-specific FILE location preflight is deliberately lexical and
    // conservative; it is not a duplicate of Precision CUE's generator.
    const auto sibling=preview_cue_sibling_locator(
        "file:///Z:/Music/Album/set.cue","audio (Extended Mix).flac");
    check(sibling.eligible &&
          sibling.source_candidate==
              "file:///Z:/Music/Album/audio (Extended Mix).flac" &&
          sibling.reason=="CUE_FILE_HOST_PROBE_REQUIRED",
          "safe sibling FILE must still require independent host identity proof");
    for(const std::string bad : {
            "../other.flac", "nested/other.flac", "nested\\other.flac",
            "C:\\other.flac", "other.flac:tag", "other?.flac",
            "CON.flac", "COM1.wav", "track%.mp3", "..", "bad.flac ",
            "evil\nname.flac"}) {
        check(!preview_cue_sibling_locator(
            "file:///Z:/Music/Album/set.cue",bad).eligible,
            "dangerous or ambiguous FILE locator must never be probed");
    }
    check(!preview_cue_sibling_locator("unqualified.cue","audio.flac").eligible,
          "CUE without qualified containing folder must fail closed");
    FieldReviewRow file_observation;
    file_observation.reason="cue_reference_preflight_only";
    file_observation.candidate.provider="Local filesystem (read-only)";
    file_observation.state=FieldReviewState::Blocked;
    check(native_candidate_source_label(file_observation)==
              "Local filesystem (read-only)" &&
          native_candidate_status_label(file_observation)=="Reference only",
          "native CUE file host observation never appears as approved edit");
    const std::string cue = "TITLE Album\nPERFORMER Album Artist\n"
        "REM GENRE House\nREM DATE 1998\n"
        "FILE x.flac WAVE\n"
        "TRACK 01 AUDIO\nTITLE First\nPERFORMER Soloist\n"
        "TRACK 02 AUDIO\nTITLE Second\n"
        "TRACK 03 AUDIO\nTITLE Third\n";
    const auto preserved = cue;
    const auto cue_hints=discogs::cue_search_hints(
        inspect_cue_metadata(cue,CueCarrierKind::EmbeddedText));
    check(cue_hints.title=="Album" && cue_hints.album=="Album" &&
          cue_hints.artist=="Album Artist" &&
          cue_hints.barcode.empty() && cue_hints.catalog_number.empty(),
          "qualified CUE global album supports official Discogs discovery only");
    const auto duplicate_album=inspect_cue_metadata(
        "TITLE \"One\"\nTITLE \"Two\"\nPERFORMER \"Composer\"\n"
        "FILE a.flac WAVE\nTRACK 01 AUDIO\nTITLE \"Track\"\n",
        CueCarrierKind::ExternalText);
    const auto uncertain_hints=discogs::cue_search_hints(duplicate_album);
    check(duplicate_album.status==CueSyntaxStatus::Parsed &&
          uncertain_hints.title.empty() && uncertain_hints.album.empty() &&
          uncertain_hints.artist=="Composer" &&
          discogs::release_id_from_url(
              "https://www.discogs.com/release/37332711-1000-Years")==37332711,
          "duplicate global CUE album does not block manually selected official edition");
    const auto parsed = inspect_cue_metadata(cue, CueCarrierKind::EmbeddedText);
    check(parsed.status == CueSyntaxStatus::Parsed, "qualified source fixture");
    // Original CUE evidence is independent from the exact online edition.
    // Never coalesce duplicate fields or promote unknown REM into a writer.
    const std::string custom_text =
        "REM GENRE House\nREM GENRE Trance\nREM CUSTOM Keep-A\n"
        "FILE \"part one.flac\" WAVE\nTRACK 01 AUDIO\n"
        "TITLE \"Only One\"\nPERFORMER \"DJ\"\n"
        "REM CUSTOM Keep-B\n";
    const auto custom=inspect_cue_metadata(
        custom_text,CueCarrierKind::ExternalText);
    check(custom.status==CueSyntaxStatus::Parsed,
          "unknown and repeated REM remain readable source facts");
    const auto originals=cue_original_inventory_rows(
        custom,"CUE Original / ");
    check(originals.size()==7 && originals[0].field==
              "CUE Original / Album / REM GENRE" &&
          originals[1].original_values==std::vector<std::string>{"Trance"} &&
          originals[2].field=="CUE Original / Album / REM CUSTOM" &&
          originals[2].original_values==std::vector<std::string>{"Keep-A"} &&
          originals[2].candidate.source_id=="cue-line:3" &&
          originals[5].field=="CUE Original / Track 1 (CUE ordinal 1, FILE #1) / REM CUSTOM" &&
          originals[5].original_values==std::vector<std::string>{"Keep-B"} &&
          originals[6].field=="CUE Original / FILE #1 / FILE" &&
          originals[6].original_values==std::vector<std::string>{"part one.flac"},
          "CUE full globals/unknown REM/duplicate values/track/FILE survive");
    for(const auto& row:originals)
        check(is_local_cue_inventory(row) &&
              row.candidate.values.empty() &&
              row.state==FieldReviewState::Unchanged &&
              row.candidate.provider.starts_with("External CUE") &&
              row.candidate.source_id.starts_with("cue-line:"),
              "original facts cannot impersonate provider or writable evidence");
    const auto catalog_originals=cue_original_inventory_rows(
        inspect_cue_metadata(
            "CATALOG 4006381333931\nTITLE Album\n"
            "FILE album.flac WAVE\nTRACK 01 AUDIO\nTITLE First\n",
            CueCarrierKind::ExternalText));
    check(!catalog_originals.empty() &&
          catalog_originals[0].field=="Album / CATALOG" &&
          catalog_originals[0].original_values==
              std::vector<std::string>{"4006381333931"} &&
          catalog_originals[0].candidate.scope==EvidenceScope::Edition &&
          catalog_originals[0].candidate.values.empty() &&
          catalog_originals[0].state==FieldReviewState::Unchanged,
          "CUE barcode is exact edition-scoped original evidence, not a provider proposal");
    const auto embedded_originals=cue_original_inventory_rows(
        inspect_cue_metadata(custom_text,CueCarrierKind::EmbeddedText));
    check(embedded_originals.size()==originals.size() &&
          embedded_originals[0].candidate.provider=="Embedded CUE" &&
          custom_text.find("Keep-B")!=std::string::npos,
          "embedded CUE has separate provenance and remains unmodified");
    const auto multi_file=inspect_cue_metadata(
        "FILE a.flac WAVE\nTRACK 01 AUDIO\nTITLE First\n"
        "FILE b.flac WAVE\nTRACK 01 AUDIO\nTITLE Second\n",
        CueCarrierKind::ExternalText);
    const auto multi_file_originals=cue_original_inventory_rows(multi_file);
    check(multi_file.status==CueSyntaxStatus::Parsed &&
          multi_file_originals.size()==4 &&
          multi_file_originals[0].field==
              "Track 1 (CUE ordinal 1, FILE #1) / TITLE" &&
          multi_file_originals[1].field==
              "Track 1 (CUE ordinal 2, FILE #2) / TITLE" &&
          multi_file_originals[0].candidate.source_id=="cue-line:3" &&
          multi_file_originals[1].candidate.source_id=="cue-line:6",
          "same declared track number across FILE blocks keeps unique local identity");
    // Multi-FILE CUEs may restart TRACK 01 for each physical source.
    // CUE ordinal remains unique, but that repeated number is NOT proof
    // that either recording is track #1 in the online release edition.
    const auto repeating_file_numbers=inspect_cue_metadata(
        "TITLE Album\nPERFORMER Example DJ\n"
        "FILE a.flac WAVE\nTRACK 01 AUDIO\nTITLE First\n"
        "FILE b.flac WAVE\nTRACK 01 AUDIO\nTITLE Second\n",
        CueCarrierKind::ExternalText);
    const auto multi_lookup=prepare_cue_release_lookup(repeating_file_numbers);
    check(multi_lookup.eligible && multi_lookup.tracks.size()==2 &&
          multi_lookup.tracks[0].source_index==0 &&
          multi_lookup.tracks[1].source_index==1 &&
          multi_lookup.tracks[0].track_number==-1 &&
          multi_lookup.tracks[1].track_number==-1 &&
          multi_lookup.tracks[0].disc_number==-1 &&
          multi_lookup.tracks[1].disc_number==-1,
          "repeated TRACK 01 across FILE blocks retains ordinals but no invented position");
    EditionTrack provider_track;
    provider_track.track_number=1;
    provider_track.disc_number=1;
    const auto unknown_position=compare_position(multi_lookup.tracks[1],provider_track);
    check(!unknown_position.conflict && unknown_position.bonus==0,
          "repeated CUE number supplies no misleading provider track match bonus");
    const auto sequential_file_numbers=inspect_cue_metadata(
        "PERFORMER Example DJ\n"
        "FILE a.flac WAVE\nTRACK 01 AUDIO\nTITLE First\n"
        "FILE b.flac WAVE\nTRACK 02 AUDIO\nTITLE Second\n",
        CueCarrierKind::ExternalText);
    const auto sequential_lookup=prepare_cue_release_lookup(sequential_file_numbers);
    check(sequential_lookup.eligible && sequential_lookup.tracks.size()==2 &&
          sequential_lookup.tracks[0].track_number==1 &&
          sequential_lookup.tracks[1].track_number==2 &&
          compare_position(sequential_lookup.tracks[0],provider_track).bonus==6,
          "unique multi-FILE CUE numbers remain valid unambiguous position evidence");
    check(cue_original_inventory_rows(repeating_file_numbers).size()==6,
          "album TITLE and PERFORMER, two track titles and two FILEs remain distinct");
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

    // Whole-vertical read-only CUE comparison: BOTH CUE tracks match their
    // provider edition IDs/ISRCs. Album tags are reviewable only while every
    // remote track carries consistent exact-edition provenance.
    std::string matched_cue=release_cue;
    const std::string second_artist="PERFORMER \"Artist B\"\n";
    const auto second_at=matched_cue.find(second_artist);
    check(second_at!=std::string::npos,
          "complete read-only CUE fixture has the second performer");
    matched_cue.insert(second_at+second_artist.size(),
                       "ISRC GBABC2600002\n");

    auto complete_edition=edition;
    complete_edition.tracks[0].recording.isrc="GBABC2600002";
    // TITLE alone, even with a matching ISRC, is insufficient to prove a
    // generic/unknown mix. Qualify the same named edition on both sides.
    const std::string old_title="TITLE Lunar\n";
    const auto title_at=matched_cue.find(old_title);
    check(title_at!=std::string::npos,
          "complete fixture contains the second recording title");
    matched_cue.replace(title_at,old_title.size(),
                        "TITLE \"Lunar (Extended Mix)\"\n");
    complete_edition.tracks[0].recording.title="Lunar (Extended Mix)";
    complete_edition.fields[0].values={"Renamed Release"};
    const auto complete_inventory=inspect_cue_metadata(
        matched_cue,CueCarrierKind::ExternalText);
    const auto complete_review=review_cue_release_edition(
        complete_inventory,complete_edition);
    check(complete_review.eligible && complete_review.tracks.size()==2 &&
          complete_review.album_fields.size()==1 &&
          complete_review.tracks[0].decision==MatchDecision::Suggested &&
          complete_review.tracks[1].decision==MatchDecision::Suggested &&
          complete_review.album_fields[0].state==FieldReviewState::NeedsReview,
          "complete unique CUE alignment permits read-only album proposal");

    // Independent negative: without a named mix, a matching ISRC is not
    // enough to authorize an album-global proposal.
    auto unknown_mix=complete_edition;
    unknown_mix.tracks[0].recording.title="Lunar";
    std::string unknown_mix_cue=matched_cue;
    const std::string known_title="TITLE \"Lunar (Extended Mix)\"\n";
    const auto known_at=unknown_mix_cue.find(known_title);
    check(known_at!=std::string::npos,"named mix fixture qualified");
    unknown_mix_cue.replace(known_at,known_title.size(),"TITLE Lunar\n");
    const auto unknown_mix_inventory=inspect_cue_metadata(
        unknown_mix_cue,CueCarrierKind::ExternalText);
    const auto unknown_review=review_cue_release_edition(
        unknown_mix_inventory,unknown_mix);
    check(unknown_review.eligible &&
          unknown_review.album_fields[0].state==FieldReviewState::Blocked,
          "matching ISRC cannot override missing mix identity");

    // Changing only a provider field ID must withdraw that proposal even
    // though both recording identities still align perfectly.
    auto cross_edition=complete_edition;
    cross_edition.tracks[0].fields[0].source_id="881/track/999";
    const auto bad_id_review=review_cue_release_edition(
        complete_inventory,cross_edition);
    check(bad_id_review.eligible &&
          bad_id_review.album_fields[0].state==FieldReviewState::Blocked &&
          bad_id_review.album_fields[0].reason.starts_with(
              "cue_edition_album_unverified_") &&
          bad_id_review.tracks[1].fields.empty(),
          "foreign track ID blocks global album changes and track fields");
    auto cross_provider=complete_edition;
    cross_provider.tracks[0].fields[0].provider="musicbrainz";
    const auto bad_provider_review=review_cue_release_edition(
        complete_inventory,cross_provider);
    check(bad_provider_review.album_fields[0].state==FieldReviewState::Blocked &&
          bad_provider_review.tracks[1].fields.empty(),
          "mixed provider provenance cannot promote CUE-global changes");
    check(complete_inventory.source_bytes==matched_cue.size(),
          "provider review cannot modify CUE bytes");

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
    const auto broken_review=review_cue_release_edition(broken_cue,edition);
    check(!broken_review.eligible && broken_review.album_fields.empty() &&
          broken_review.tracks.empty(),
          "invalid CUE syntax cannot present even album candidates");
    // End-to-end: official Discogs edition JSON -> CUE text -> one-to-one
    // association -> comparison. Missing CUE runtime/ISRC must show BLOCKED
    // per-field evidence, not falsely accepted metadata.
    const std::string no_duration_cue=
        "TITLE \"Night EP\"\nPERFORMER \"Example\"\n"
        "FILE \"mix.flac\" WAVE\n"
        "TRACK 01 AUDIO\nTITLE \"Home (Extended Mix)\"\n"
        "INDEX 01 00:00:00\n"
        "TRACK 02 AUDIO\nTITLE \"Away (Extended Mix)\"\n"
        "INDEX 01 06:10:00\n";
    const auto no_duration_inventory=inspect_cue_metadata(
        no_duration_cue,CueCarrierKind::ExternalText);
    const auto official_edition=discogs::parse_release(R"DISCOGS({
        "id":38012673,"title":"Night EP",
        "artists":[{"name":"Example"}],
        "tracklist":[
            {"position":"1","title":"Home (Extended Mix)","duration":"6:10",
             "extraartists":[{"name":"Remixer","role":"Remix"}]},
            {"position":"2","title":"Away (Extended Mix)","duration":"5:20"}
        ]
    })DISCOGS",38012673);
    // Valid CUE-global album fields remain useful even if one CUE track
    // lacks PERFORMER, but ALL remote candidates must be display-only BLOCKED.
    const std::string incomplete_identity_cue =
        "TITLE \"Night EP\"\nREM GENRE House\n"
        "FILE \"mix.flac\" WAVE\n"
        "TRACK 01 AUDIO\nTITLE \"Home (Extended Mix)\"\n"
        "PERFORMER Example\nINDEX 01 00:00:00\n"
        "TRACK 02 AUDIO\nTITLE \"Away (Extended Mix)\"\n"
        "INDEX 01 06:10:00\n";
    const auto incomplete_inventory=inspect_cue_metadata(
        incomplete_identity_cue,CueCarrierKind::EmbeddedText);
    const auto incomplete_review=review_cue_release_edition(
        incomplete_inventory,official_edition);
    check(!incomplete_review.eligible &&
          incomplete_review.reasons==
              std::vector<std::string>{"cue_track_identity_missing_or_ambiguous"} &&
          incomplete_review.album_fields.size()>=2 &&
          incomplete_review.tracks.size()==2 &&
          incomplete_review.album_fields.front().candidate.source_id=="38012673" &&
          incomplete_review.album_fields.front().state==FieldReviewState::Blocked &&
          incomplete_review.album_fields.front().reason.starts_with(
              "cue_track_inventory_unqualified_") &&
          incomplete_review.tracks[0].fields.empty() &&
          incomplete_review.tracks[1].fields.empty() &&
          incomplete_review.tracks[0].unverified_fields.empty() &&
          incomplete_review.tracks[1].unverified_fields.empty(),
          "incomplete CUE track identity retains blocked album-only provenance");
    auto spoofed_album=official_edition;
    spoofed_album.fields[0].source_id="foreign-release";
    const auto untrusted_incomplete=review_cue_release_edition(
        incomplete_inventory,spoofed_album);
    check(!untrusted_incomplete.eligible &&
          untrusted_incomplete.album_fields.empty() &&
          untrusted_incomplete.tracks.empty(),
          "unqualified edition provenance never leaks via CUE fallback");
    check(incomplete_inventory.source_bytes==incomplete_identity_cue.size(),
          "album-only fallback never changes the original embedded CUE bytes");
    const auto evidence_only=review_cue_release_edition(
        no_duration_inventory,official_edition);
    check(evidence_only.eligible && evidence_only.tracks.size()==2 &&
          evidence_only.tracks[0].decision==MatchDecision::Review &&
          evidence_only.tracks[0].fields.empty() &&
          evidence_only.tracks[0].unverified_fields.size()>=2,
          "missing CUE duration does not fabricate a qualified track match");
    const auto& preview=evidence_only.tracks[0].unverified_fields.front();
    check(preview.cue_field=="TITLE" &&
          preview.original_values==std::vector<std::string>{"Home (Extended Mix)"} &&
          preview.candidate.source_id=="38012673/track/1" &&
          preview.state==FieldReviewState::Blocked &&
          preview.reason.starts_with("cue_unverified_alignment_evidence_only_"),
          "blocked Discogs track review preserves exact edition provenance and original");
    for(const auto& track:evidence_only.tracks)
        for(const auto& candidate:track.unverified_fields)
            check(candidate.state==FieldReviewState::Blocked,
                  "weak edition fields never become actionable");
    auto forged_edition=official_edition;
    forged_edition.tracks[0].fields[0].source_id="38012673/track/99";
    const auto forged_review=review_cue_release_edition(
        no_duration_inventory,forged_edition);
    check(forged_review.tracks[0].fields.empty() &&
          forged_review.tracks[0].unverified_fields.empty(),
          "forged Discogs track provenance cannot enter blocked display");
    auto wrong_remix=official_edition;
    wrong_remix.tracks[0].recording.title="Home (Radio Edit)";
    const auto incompatible=review_cue_release_edition(
        no_duration_inventory,wrong_remix);
    check(incompatible.tracks[0].unverified_fields.empty(),
          "wrong remix never receives provisional track field evidence");
    // A real Discogs edition is authenticated catalog evidence, not proof
    // that this CUE has the same complete local tracklist. Never make an
    // album-global change appear reviewable on uncertain track alignment.
    auto different_album=official_edition;
    bool has_album=false;
    for(auto& source_field:different_album.fields)
        if(source_field.field=="ALBUM" && !source_field.values.empty()) {
            source_field.values={"Unrelated Edition"};
            has_album=true;
            break;
        }
    check(has_album,"Discogs fixture contains exact edition album evidence");
    const auto blocked_album=review_cue_release_edition(
        no_duration_inventory,different_album);
    bool protected_album=false;
    for(const auto& item:blocked_album.album_fields)
        if(item.candidate.field=="ALBUM" &&
           item.state==FieldReviewState::Blocked &&
           item.reason.starts_with("cue_edition_album_unverified_"))
            protected_album=true;
    check(blocked_album.eligible && protected_album,
          "incomplete Discogs-CUE track alignment blocks global album changes");
    const auto nochange=std::find_if(evidence_only.album_fields.begin(),
        evidence_only.album_fields.end(),
        [](const auto& item){return item.candidate.field=="ALBUM";});
    check(nochange!=evidence_only.album_fields.end() &&
          nochange->state==FieldReviewState::Unchanged,
          "identical album facts remain safe no-ops despite uncertain tracks");
    check(no_duration_inventory.source_bytes==no_duration_cue.size(),
          "read-only release review never patches CUE bytes");
    std::cout << "PASS: CUE field scope, inheritance impact, no-op, unknown-field guards\n";
}
