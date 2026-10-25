#include "djmeta/cue_review_postimage.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
namespace {
void check(bool yes,const char* msg){
    if(!yes){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
}
int main(){
    using namespace djmeta;
    using namespace djmeta::online;
    const std::string raw=std::string("\xef\xbb\xbf")+
        "REM UNKNOWN \"keep\"\r\n"
        "TITLE \"Original Album\"\r\n"
        "PERFORMER \"Global DJ\"\r\n"
        "FILE \"Album.flac\" WAVE\r\n"
        " TRACK 01 AUDIO\r\n"
        " TITLE \"Original Track\"\r\n"
        " INDEX 01 00:00:00\r\n";
    const std::string mb="00000000-0000-0000-0000-000000000001";
    const std::string occurrence="00000000-0000-0000-0000-000000000002";
    const CuePostimageEvidence global{std::nullopt,
        {"ALBUM",{"Revised Album"},"musicbrainz",mb,EvidenceScope::Edition},
        false,true};
    const CuePostimageEvidence track{0,
        {"TITLE",{"Revised Track"},"musicbrainz",occurrence,EvidenceScope::Edition},
        true,false};
    const auto snap=capture_cue_postimage_review(raw,CueCarrierKind::ExternalText,
                                                  {global,track});
    check(snap.eligible && !snap.verified_edition &&
          snap.rows.size()==2 &&
          snap.rows[0].reviewed.state==FieldReviewState::NeedsReview &&
          snap.rows[1].reviewed.state==FieldReviewState::NeedsReview,
          "two independent qualified comparison rows");
    const auto selected=preview_selected_cue_review_postimage(snap,raw,{1,0});
    const std::string expected=std::string("\xef\xbb\xbf")+
        "REM UNKNOWN \"keep\"\r\n"
        "TITLE \"Revised Album\"\r\n"
        "PERFORMER \"Global DJ\"\r\n"
        "FILE \"Album.flac\" WAVE\r\n"
        " TRACK 01 AUDIO\r\n"
        " TITLE \"Revised Track\"\r\n"
        " INDEX 01 00:00:00\r\n";
    check(selected.eligible && selected.changed && selected.bytes==expected,
          "explicit album + track changes preserve all unrelated CUE bytes");
    const auto empty=preview_selected_cue_review_postimage(snap,raw,{});
    check(empty.eligible && !empty.changed && empty.bytes==raw,
          "zero selection is byte-identical no-op");
    const auto track_only=preview_selected_cue_review_postimage(snap,raw,{1});
    check(track_only.eligible && track_only.changed &&
          track_only.bytes.find("TITLE \"Original Album\"")!=std::string::npos &&
          track_only.bytes.find("TITLE \"Revised Track\"")!=std::string::npos,
          "selection never implies other rows");
    // Detail table may be sorted independently of the immutable CUE rows;
    // multiple highlighted fields must still stage exactly their own evidence.
    auto display=[](const CueCandidateFieldReview& row) {
        FieldReviewRow out;
        out.field=row.cue_field;
        out.original_values=row.original_values;
        out.candidate=row.candidate;
        out.state=row.state;
        return out;
    };
    std::vector<FieldReviewRow> display_reversed{
        display(snap.rows[1].reviewed),display(snap.rows[0].reviewed)};
    const auto sorted_selection=resolve_cue_detail_selection(
        snap,display_reversed,{0,1});
    check(sorted_selection==std::vector<std::size_t>({1,0}),
          "sorted multi-select maps to exact provider evidence identities");
    check(cue_detail_row_in_preview(snap,sorted_selection,display_reversed[0]) &&
          cue_detail_row_in_preview(snap,sorted_selection,display_reversed[1]) &&
          !cue_detail_row_in_preview(snap,{1},display_reversed[1]) &&
          !cue_detail_row_in_preview(snap,{1,1},display_reversed[0]) &&
          !cue_detail_row_in_preview(snap,{100},display_reversed[0]),
          "preview markers match immutable rows under sorting, reject forged IDs");
    const auto grouped_preview=preview_selected_cue_review_postimage(
        snap,raw,sorted_selection);
    check(grouped_preview.eligible && grouped_preview.changed &&
          grouped_preview.bytes==expected,
          "multi-selected CUE detail values are previewed in one atomic byte postimage");
    const auto invalid_detail=[&](const auto& displayed,
                                  const std::vector<std::size_t>& selection) {
        try {(void)resolve_cue_detail_selection(snap,displayed,selection);}
        catch(const std::invalid_argument&) {return true;}
        return false;
    };
    check(invalid_detail(display_reversed,{}) &&
          invalid_detail(display_reversed,{0,0}) &&
          invalid_detail(display_reversed,{999}),
          "empty, duplicate and stale selected UI rows reject the whole preview");
    auto forged_display=display_reversed;
    forged_display[0].candidate.values={"forged different provider value"};
    check(invalid_detail(forged_display,{0,1}),
          "one changed provider field prevents every multi-selection write plan");
    forged_display=display_reversed;
    forged_display.push_back(forged_display[0]);
    check(invalid_detail(forged_display,{0,2}),
          "two visible rows cannot independently claim the same immutable CUE proposal");
    const auto reject=[&](const CuePostimageReviewSnapshot& s,
                          const std::string& data,
                          std::vector<std::size_t> indices){
        auto v=preview_selected_cue_review_postimage(s,data,indices);
        return !v.eligible && !v.changed && v.bytes.empty() &&
               !v.rejection.empty();
    };
    check(reject(snap,raw+" ",{0}),"concurrently edited CUE rejected");
    check(reject(snap,raw,{0,0}),"duplicate selection rejected");
    check(reject(snap,raw,{2}),"invalid selection rejected");
    auto tampered=snap;
    tampered.rows[1].reviewed.original_values={"forged"};
    check(reject(tampered,raw,{1}),"mutated review summary rejected");
    tampered=snap;
    tampered.rows[0].reviewed.state=FieldReviewState::Unchanged;
    check(reject(tampered,raw,{0}),"forged no-op cannot hide changed evidence");
    const auto no_alignment=capture_cue_postimage_review(raw,
        CueCarrierKind::ExternalText,{{std::nullopt,global.evidence,false,false}});
    check(reject(no_alignment,raw,{0}),"partial release cannot alter album-global");
    const CuePostimageEvidence inherited{0,
        {"ARTIST",{"Other DJ"},"musicbrainz",occurrence,EvidenceScope::Edition},
        true,false};
    const auto inherit=capture_cue_postimage_review(raw,
        CueCarrierKind::EmbeddedText,{inherited});
    check(inherit.eligible && inherit.rows[0].reviewed.original_inherited &&
          reject(inherit,raw,{0}),"inherited track value needs insertion, blocked");
    const CuePostimageEvidence absent{0,
        {"SONGWRITER",{"Someone"},"musicbrainz",occurrence,EvidenceScope::Edition},
        true,false};
    const auto missing=capture_cue_postimage_review(raw,
        CueCarrierKind::ExternalText,{absent});
    check(missing.eligible && reject(missing,raw,{0}),
          "missing token is not silently inserted");
    const CuePostimageEvidence multiple{0,
        {"TITLE",{"One","Two"},"musicbrainz",occurrence,EvidenceScope::Edition},
        true,false};
    const auto multi=capture_cue_postimage_review(raw,
        CueCarrierKind::ExternalText,{multiple});
    check(multi.eligible && reject(multi,raw,{0}),
          "multivalue evidence cannot collapse to one CUE token");
    const CuePostimageEvidence unchanged{0,
        {"TITLE",{"Original Track"},"musicbrainz",occurrence,EvidenceScope::Edition},
        true,false};
    const auto same=capture_cue_postimage_review(raw,CueCarrierKind::EmbeddedText,
                                                 {unchanged});
    const auto exact_noop=preview_selected_cue_review_postimage(same,raw,{0});
    check(exact_noop.eligible && !exact_noop.changed && exact_noop.bytes==raw,
          "explicit equal evidence is still a no-op");
    const auto bad_source=capture_cue_postimage_review(
        "FILE x.flac WAVE\nTRACK 01 AUDIO\nTITLE \"broken\n",
        CueCarrierKind::ExternalText,{track});
    check(!bad_source.eligible && reject(bad_source,raw,{0}),
          "invalid source CUE cannot be staged");

    // The release-bound adapter recomputes one-to-one identity from actual
    // CUE bytes + the exact provider edition, rather than trusting UI booleans.
    const std::string release_raw=
        "TITLE \"Local album\"\n"
        "PERFORMER \"DJ\"\n"
        "FILE \"set.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "TITLE \"Sunrise (Extended Mix)\"\n"
        "PERFORMER \"DJ\"\n"
        "ISRC GBABC2600001\n"
        "INDEX 01 00:00:00\n";
    ReleaseEdition mb_edition;
    mb_edition.provider="musicbrainz";
    mb_edition.edition_id=mb;
    mb_edition.fields.push_back({"ALBUM",{"Other album"},
        "musicbrainz",mb,EvidenceScope::Edition});
    EditionTrack mb_track;
    mb_track.source_track_id=occurrence;
    mb_track.recording={"Sunrise (Extended Mix)","DJ","",
        MixKind::Extended,"GBABC2600001",-1};
    mb_track.track_number=1;
    mb_track.disc_number=1;
    mb_track.fields.push_back({"TITLE",{"Corrected Sunrise (Extended Mix)"},
        "musicbrainz",occurrence,EvidenceScope::Edition});
    mb_edition.tracks.push_back(mb_track);
    const auto qualified=capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,mb_edition);
    check(qualified.eligible && qualified.verified_edition &&
          qualified.rows.size()==2 &&
          qualified.rows[0].reviewed.state==FieldReviewState::NeedsReview &&
          qualified.rows[1].reviewed.state==FieldReviewState::NeedsReview,
          "exact MB release and verified occurrence expose two review rows");
    const auto mb_preview=preview_selected_cue_review_postimage(
        qualified,release_raw,{0,1});
    const std::string mb_expected=
        "TITLE \"Other album\"\n"
        "PERFORMER \"DJ\"\n"
        "FILE \"set.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "TITLE \"Corrected Sunrise (Extended Mix)\"\n"
        "PERFORMER \"DJ\"\n"
        "ISRC GBABC2600001\n"
        "INDEX 01 00:00:00\n";
    check(mb_preview.eligible && mb_preview.changed &&
          mb_preview.bytes==mb_expected,
          "exact release bridge preserves all non-selected bytes");
    // Exercise the actual official JSON parser, not only hand-built evidence.
    // Its release-track occurrence must carry the same field provenance that
    // the native comparison and selected CUE postimage both require.
    const auto parsed_mb=musicbrainz::parse_release_lookup(R"MB({
      "id":"00000000-0000-0000-0000-000000000001",
      "title":"Other album","artist-credit":[{"name":"DJ"}],
      "media":[{"tracks":[{"id":"00000000-0000-0000-0000-000000000002",
        "position":1,"title":"Sunrise (Extended Mix)",
        "artist-credit":[{"name":"DJ"}],
        "recording":{"title":"Sunrise (Extended Mix)",
          "isrcs":["GBABC2600001"]}}]}]
    })MB",mb);
    check(parsed_mb.tracks.size()==1 &&
          parsed_mb.tracks[0].fields.size()==2 &&
          parsed_mb.tracks[0].fields[0].source_id==occurrence &&
          parsed_mb.tracks[0].fields[0].scope==EvidenceScope::Edition,
          "official MusicBrainz parser supplies occurrence-bound CUE evidence");
    const auto parsed_snapshot=capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,parsed_mb);
    check(parsed_snapshot.eligible && parsed_snapshot.rows.size()>=1 &&
          parsed_snapshot.rows[0].reviewed.candidate.field=="ALBUM",
          "actual parsed release crosses exact-edition CUE postimage gate");
    // A search hit with an otherwise matching tracklist cannot stage a
    // postimage against a different exact-edition disc barcode.
    const std::string coded_release_raw=
        "CATALOG 4006381333931\n"+release_raw;
    auto coded_mb=mb_edition;
    coded_mb.fields.push_back({"BARCODE",{"4006381333931"},
        "musicbrainz",mb,EvidenceScope::Edition});
    const auto coded_same=capture_qualified_cue_edition_postimage(
        coded_release_raw,CueCarrierKind::ExternalText,coded_mb);
    check(coded_same.eligible && coded_same.rows.size()==2,
          "matching CUE and MusicBrainz media codes preserve read-only postimage");
    coded_mb.fields.back().values={"036000291452"};
    const auto coded_conflict=capture_qualified_cue_edition_postimage(
        coded_release_raw,CueCarrierKind::ExternalText,coded_mb);
    check(!coded_conflict.eligible && coded_conflict.rows.empty(),
          "different exact-edition barcode cannot stage album or track changes");
    auto mb_bad=mb_edition;
    mb_bad.tracks[0].source_track_id="unqualified";
    check(!capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,mb_bad).eligible,
        "invalid MusicBrainz occurrence cannot enter selected CUE postimage");
    mb_bad=mb_edition;
    mb_bad.tracks[0].fields[0].source_id=mb;
    check(!capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,mb_bad).eligible,
        "album MBID cannot impersonate a release-track occurrence");
    // A provider field cannot borrow the identity of a different release
    // occurrence, even when TITLE/ISRC/positions still align perfectly.
    mb_bad=mb_edition;
    mb_bad.tracks[0].source_track_id=
        "00000000-0000-0000-0000-000000000004";
    const auto mb_mismatch=review_cue_release_edition(
        inspect_cue_metadata(release_raw,CueCarrierKind::ExternalText),
        mb_bad);
    check(!qualified_cue_edition_track_provenance(mb_bad,0) &&
          mb_mismatch.tracks.size()==1 &&
          mb_mismatch.tracks[0].fields.empty() &&
          !capture_qualified_cue_edition_postimage(
              release_raw,CueCarrierKind::ExternalText,mb_bad).eligible,
          "foreign MusicBrainz occurrence cannot inherit valid field provenance");
    mb_bad=mb_edition;
    mb_bad.tracks[0].fields.push_back(mb_bad.tracks[0].fields[0]);
    check(!capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,mb_bad).eligible,
        "duplicate provider track field cannot enter selected CUE postimage");
    mb_bad=mb_edition;
    mb_bad.tracks[0].recording.title="Sunrise (Radio Edit)";
    const auto wrong_mix=capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,mb_bad);
    check(!wrong_mix.eligible || wrong_mix.rows.empty(),
          "wrong remix cannot produce editable edition track fields");
    mb_bad=mb_edition;
    mb_bad.tracks.push_back(mb_track);
    mb_bad.tracks[1].source_track_id=
        "00000000-0000-0000-0000-000000000003";
    mb_bad.tracks[1].fields[0].source_id=
        mb_bad.tracks[1].source_track_id;
    const auto partial=capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::ExternalText,mb_bad);
    check(!partial.eligible || partial.rows.empty() ||
          partial.rows[0].input.evidence.field!="ALBUM",
          "partial edition never exposes changed album-global row");
    ReleaseEdition discogs_edition=mb_edition;
    discogs_edition.provider="discogs";
    discogs_edition.edition_id="123";
    discogs_edition.fields[0].provider="discogs";
    discogs_edition.fields[0].source_id="123";
    discogs_edition.tracks[0].source_track_id="123/track/1";
    discogs_edition.tracks[0].fields[0].provider="discogs";
    discogs_edition.tracks[0].fields[0].source_id="123/track/1";
    const auto discogs_snap=capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::EmbeddedText,discogs_edition);
    check(discogs_snap.eligible && discogs_snap.rows.size()==2 &&
          preview_selected_cue_review_postimage(
              discogs_snap,release_raw,{0,1}).bytes==mb_expected,
          "Discogs exact edition uses same byte-exact external/embedded core");
    auto discogs_wrong_track=discogs_edition;
    discogs_wrong_track.tracks[0].source_track_id="123/track/9";
    const auto discogs_identity_review=review_cue_release_edition(
        inspect_cue_metadata(release_raw,CueCarrierKind::EmbeddedText),
        discogs_wrong_track);
    check(!qualified_cue_edition_track_provenance(discogs_wrong_track,0) &&
          discogs_identity_review.tracks.size()==1 &&
          discogs_identity_review.tracks[0].fields.empty() &&
          !capture_qualified_cue_edition_postimage(
              release_raw,CueCarrierKind::EmbeddedText,
              discogs_wrong_track).eligible,
          "foreign Discogs occurrence cannot inherit valid field provenance");
    discogs_edition.tracks[0].fields[0].source_id="999/track/1";
    const auto forged=capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::EmbeddedText,discogs_edition);
    check(!forged.eligible || forged.rows.empty() ||
          (forged.rows.size()==1 &&
           forged.rows[0].input.evidence.field!="TITLE"),
          "foreign Discogs edition-track field never becomes selectable");
    discogs_edition.provider="unsupported";
    check(!capture_qualified_cue_edition_postimage(
        release_raw,CueCarrierKind::EmbeddedText,discogs_edition).eligible,
        "unverified provider cannot claim edition qualification");

    // A fully verified large edition must never yield a silently truncated
    // eligible snapshot once its reviewable rows exceed the 512-field cap.
    std::string large_raw=
        "TITLE \"Large album\"\nPERFORMER \"DJ\"\nFILE \"large.flac\" WAVE\n";
    ReleaseEdition large;
    large.provider="musicbrainz";
    large.edition_id=mb;
    large.fields.push_back({"ALBUM",{"Large album"},
        "musicbrainz",mb,EvidenceScope::Edition});
    for(int n=1;n<=128;++n) {
        const auto title="Track "+std::to_string(n)+" (Extended Mix)";
        const auto suffix=std::to_string(n);
        const auto isrc="GBABC26"+std::string(5-suffix.size(),'0')+suffix;
        large_raw+="TRACK "+std::to_string(n)+" AUDIO\n"
            "TITLE \""+title+"\"\nPERFORMER \"DJ\"\n"
            "SONGWRITER \"Writer\"\nREM COMMENT \"Note\"\n"
            "ISRC "+isrc+"\nINDEX 01 00:00:00\n";
        EditionTrack item;
        item.source_track_id="00000000-0000-0000-0000-"+
            std::string(12-suffix.size(),'0')+suffix;
        item.recording={title,"DJ","",MixKind::Extended,isrc,-1};
        item.disc_number=1;
        item.track_number=n;
        for(const auto& [field,value]:
            std::vector<std::pair<std::string,std::string>>{
                {"TITLE",title},{"ARTIST","DJ"},
                {"SONGWRITER","Writer"},{"COMMENT","Note"}})
            item.fields.push_back({field,{value},"musicbrainz",
                item.source_track_id,EvidenceScope::Edition});
        large.tracks.push_back(std::move(item));
    }
    const auto large_cue=inspect_cue_metadata(
        large_raw,CueCarrierKind::ExternalText);
    const auto large_local=prepare_cue_release_lookup(large_cue);
    check(large_local.eligible && large_local.tracks.size()==128,
          "oversized CUE fixture parses and supplies exact track inventory");
    const auto large_alignment=align_release(large_local.tracks,large);
    check(qualified_musicbrainz_cue_album_alignment(
        large_cue,large,large_alignment),
          "oversized CUE fixture independently qualifies full edition identity");
    const auto large_review=review_cue_release_edition(large_cue,large);
    std::size_t reviewable=0;
    for(const auto& field:large_review.album_fields)
        if(field.state==FieldReviewState::NeedsReview ||
           field.state==FieldReviewState::Unchanged) ++reviewable;
    for(const auto& review_track:large_review.tracks)
        for(const auto& field:review_track.fields)
            if(field.state==FieldReviewState::NeedsReview ||
               field.state==FieldReviewState::Unchanged) ++reviewable;
    check(reviewable>512,"oversized CUE fixture has more than 512 reviewable fields");
    const auto large_snapshot=capture_qualified_cue_edition_postimage(
        large_raw,CueCarrierKind::ExternalText,large);
    check(!large_snapshot.eligible && large_snapshot.rows.empty(),
          "over-limit qualified CUE review fails closed instead of truncating");

    std::cout<<"PASS: explicit CUE field selection, source recheck, safe byte postimage\n";
}
