#include "djmeta/cue_musicbrainz_release_gate.h"
#include "djmeta/cue_online_review.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace djmeta;
using namespace djmeta::online;
void check(bool b,const char* msg){if(!b){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}}
int main(){
    const std::string edition_id="00000000-0000-0000-0000-000000000001";
    const std::string track_id="00000000-0000-0000-0000-000000000002";
    const auto cue=inspect_cue_metadata(
        "TITLE \"Local album\"\nPERFORMER \"DJ\"\nFILE \"set.flac\" WAVE\n"
        "TRACK 01 AUDIO\nTITLE \"Sunrise (Extended Mix)\"\n"
        "PERFORMER \"DJ\"\nISRC GBABC2600001\nINDEX 01 00:00:00\n",
        CueCarrierKind::EmbeddedText);
    ReleaseEdition edition;
    edition.provider="musicbrainz";
    edition.edition_id=edition_id;
    edition.fields.push_back({"ALBUM",{"Other album"},"musicbrainz",
                              edition_id,EvidenceScope::Edition});
    EditionTrack track;
    track.source_track_id=track_id;
    track.recording={"Sunrise (Extended Mix)","DJ","",MixKind::Extended,
                     "GBABC2600001",-1};
    track.track_number=1;
    track.disc_number=1;
    track.fields.push_back({"TITLE",{"Sunrise (Extended Mix)"},
        "musicbrainz",track_id,EvidenceScope::Edition});
    track.fields.push_back({"ARTIST",{"DJ"},
        "musicbrainz",track_id,EvidenceScope::Edition});
    edition.tracks.push_back(track);
    const auto local=prepare_cue_release_lookup(cue);
    check(local.eligible && local.tracks.size()==1,"source cue qualifies");
    const auto alignment=align_release(local.tracks,edition);
    check(alignment.tracks.size()==1 &&
          alignment.tracks[0].decision==MatchDecision::Suggested,
          "known named mix and ISRC are independently suggested");
    check(qualified_musicbrainz_cue_album_alignment(cue,edition,alignment),
          "complete exact edition qualifies source-only album comparison");
    auto distinct_track_title=edition;
    distinct_track_title.tracks[0].fields[0].values={
        "Corrected Sunrise (Extended Mix)"};
    check(qualified_musicbrainz_cue_album_alignment(
        cue,distinct_track_title,alignment),
        "direct release-track title may differ from recording match identity");
    auto recording_only=edition;
    recording_only.tracks[0].fields.clear();
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,recording_only,alignment),
        "recording facts and valid track ID without exact track fields cannot qualify album");
    auto artist_only=edition;
    artist_only.tracks[0].fields.erase(artist_only.tracks[0].fields.begin());
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,artist_only,alignment),
        "missing exact release-track TITLE defeats otherwise plausible album match");
    auto foreign_title=edition;
    foreign_title.tracks[0].fields[0].source_id=edition_id;
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,foreign_title,alignment),
        "release MBID cannot impersonate track-title occurrence evidence");
    auto duplicate_title=edition;
    duplicate_title.tracks[0].fields.push_back({"TITLE",{"Other song"},
        "musicbrainz",track_id,EvidenceScope::Edition});
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,duplicate_title,alignment),
        "two conflicting edition-track TITLE occurrences cannot qualify album");
    // The native album gate must honor the same media-identity conflict.
    const auto coded_cue=inspect_cue_metadata(
        "CATALOG 4006381333931\nTITLE \"Local album\"\n"
        "PERFORMER \"DJ\"\nFILE \"set.flac\" WAVE\n"
        "TRACK 01 AUDIO\nTITLE \"Sunrise (Extended Mix)\"\n"
        "PERFORMER \"DJ\"\nISRC GBABC2600001\nINDEX 01 00:00:00\n",
        CueCarrierKind::EmbeddedText);
    auto coded_edition=edition;
    coded_edition.fields.push_back({"BARCODE",{"4006381333931"},
        "musicbrainz",edition_id,EvidenceScope::Edition});
    check(qualified_musicbrainz_cue_album_alignment(
        coded_cue,coded_edition,alignment),
        "equal exact-edition media barcode retains full album alignment");
    coded_edition.fields.back().values={"036000291452"};
    check(!qualified_musicbrainz_cue_album_alignment(
        coded_cue,coded_edition,alignment),
        "different exact-edition media barcode defeats plausible track alignment");
    coded_edition.fields.pop_back();
    check(qualified_musicbrainz_cue_album_alignment(
        coded_cue,coded_edition,alignment),
        "missing provider barcode remains unknown, not a fabricated conflict");

    // An MB release-track occurrence ID belongs to EDITION evidence.
    // Its TITLE/ARTIST compare against that exact local CUE track through
    // the dedicated edition-track mapper, never the physical recording tag.
    FieldEvidence occurrence{"TITLE",{"Sunrise (Extended Mix)"},
        "musicbrainz",track_id,EvidenceScope::Edition};
    const auto local_fields=review_cue_candidate_fields(
        cue,0,{occurrence},true);
    check(local_fields.size()==1 &&
          local_fields[0].target==CueReviewTarget::TrackLocal &&
          local_fields[0].state==FieldReviewState::Unchanged &&
          local_fields[0].cue_field=="TITLE" &&
          local_fields[0].track_ordinal==0,
          "verified release-track occurrence maps only to CUE track-local TITLE");
    occurrence.values={"Sunrise (Radio Edit)"};
    const auto changed_fields=review_cue_candidate_fields(
        cue,0,{occurrence},true);
    check(changed_fields[0].state==FieldReviewState::NeedsReview &&
          changed_fields[0].target==CueReviewTarget::TrackLocal,
          "a changed exact edition-track value remains review-only and scoped");
    auto missing=edition;
    missing.tracks[0].source_track_id.clear();
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,missing,align_release(local.tracks,missing)),
          "missing release-track MBID rejects whole album");
    auto different=edition;
    different.tracks[0].source_track_id="invalid-MBID";
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,different,align_release(local.tracks,different)),
          "unverified release-track identity blocks album");
    auto partial=edition;
    partial.tracks.push_back(track);
    partial.tracks[1].source_track_id="00000000-0000-0000-0000-000000000003";
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,partial,align_release(local.tracks,partial)),
          "partial CUE/remote tracklists cannot propose global edit");
    auto forged=edition;
    forged.fields[0].source_id="00000000-0000-0000-0000-000000000004";
    check(!qualified_musicbrainz_cue_album_alignment(cue,forged,alignment),
          "mismatched album field provenance blocks");
    auto mismatch=edition;
    mismatch.tracks[0].recording.title="Sunrise (Radio Edit)";
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,mismatch,align_release(local.tracks,mismatch)),
          "conflicting mix identity never approves");
    auto wrong_kind=edition;
    wrong_kind.provider="discogs";
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,wrong_kind,align_release(local.tracks,wrong_kind)),
          "other provider cannot claim MB identity");
    auto wrong_alignment=alignment;
    wrong_alignment.tracks[0].decision=MatchDecision::Review;
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,edition,wrong_alignment),"review match is not a confirmed track");
    wrong_alignment=alignment;
    wrong_alignment.tracks[0].conflicts.push_back("mix_identity_unverified");
    check(!qualified_musicbrainz_cue_album_alignment(
        cue,edition,wrong_alignment),"conflict-bearing match blocked");
    const auto unparsed=inspect_cue_metadata(
        "FILE set.flac WAVE\nTRACK 01 AUDIO\nTITLE \"broken\n",
        CueCarrierKind::ExternalText);
    check(!qualified_musicbrainz_cue_album_alignment(
        unparsed,edition,alignment),"malformed CUE blocked");
    std::cout<<"PASS: MusicBrainz CUE album proposals require complete source-provenance verified release alignment\n";
}
