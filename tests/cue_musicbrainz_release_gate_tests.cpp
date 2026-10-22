#include "djmeta/cue_musicbrainz_release_gate.h"
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
    edition.tracks.push_back(track);
    const auto local=prepare_cue_release_lookup(cue);
    check(local.eligible && local.tracks.size()==1,"source cue qualifies");
    const auto alignment=align_release(local.tracks,edition);
    check(alignment.tracks.size()==1 &&
          alignment.tracks[0].decision==MatchDecision::Suggested,
          "known named mix and ISRC are independently suggested");
    check(qualified_musicbrainz_cue_album_alignment(cue,edition,alignment),
          "complete exact edition qualifies source-only album comparison");
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
