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
    check(snap.eligible && snap.rows.size()==2 &&
          snap.rows[0].review.state==FieldReviewState::NeedsReview &&
          snap.rows[1].review.state==FieldReviewState::NeedsReview,
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
    tampered.rows[1].review.original_values={"forged"};
    check(reject(tampered,raw,{1}),"mutated review summary rejected");
    tampered=snap;
    tampered.rows[0].review.state=FieldReviewState::Unchanged;
    check(reject(tampered,raw,{0}),"forged no-op cannot hide changed evidence");
    const auto no_alignment=capture_cue_postimage_review(raw,
        CueCarrierKind::ExternalText,{{std::nullopt,global.evidence,false,false}});
    check(reject(no_alignment,raw,{0}),"partial release cannot alter album-global");
    const CuePostimageEvidence inherited{0,
        {"ARTIST",{"Other DJ"},"musicbrainz",occurrence,EvidenceScope::Edition},
        true,false};
    const auto inherit=capture_cue_postimage_review(raw,
        CueCarrierKind::EmbeddedText,{inherited});
    check(inherit.eligible && inherit.rows[0].review.original_inherited &&
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
    std::cout<<"PASS: explicit CUE field selection, source recheck, safe byte postimage\n";
}
