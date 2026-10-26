#include "djmeta/precision_cue_flac_file_preview.h"
#include "djmeta/precision_cue.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"Precision CUE FLAC postimage failure line %d: %s\n",\
        __LINE__,#x); std::exit(1); } } while(false)
namespace {
void le32(std::string& out,unsigned n) {
    for (unsigned i=0; i<4; ++i) out.push_back(static_cast<char>((n>>(i*8))&255));
}
std::string block(unsigned type,const std::string& body,bool last=false) {
    std::string out(1,static_cast<char>(type|(last ? 128u:0u)));
    const auto n=static_cast<unsigned>(body.size());
    out.push_back(static_cast<char>((n>>16)&255));
    out.push_back(static_cast<char>((n>>8)&255));
    out.push_back(static_cast<char>(n&255));
    return out+body;
}
std::string vorbis(std::vector<std::string> comments) {
    std::string out;
    le32(out,10);
    out+="DJ Foobar!";
    le32(out,static_cast<unsigned>(comments.size()));
    for (auto& field:comments) {
        le32(out,static_cast<unsigned>(field.size()));
        out+=field;
    }
    return out;
}
PrecisionCuePlan cue(std::string title) {
    PrecisionCueDisc disc;
    disc.album_title=std::move(title);
    disc.album_performer="DJ Example";
    // The production Precision CUE generator intentionally accepts MP3
    // basenames only. For a FLAC *metadata* parser test, derive synthetic
    // text from that verified generator and swap only this fictitious FILE.
    disc.audio_file_basename="continuous.mp3";
    disc.genre="Electronic";
    disc.release_date="2026";
    disc.tracks.push_back({"Part One","A",119131});
    disc.tracks.push_back({"Part Two","B",125249});
    auto plan=make_precision_cue(disc);
    const auto at=plan.external_cue_utf8.find("continuous.mp3");
    CHECK(at!=std::string::npos);
    plan.external_cue_utf8.replace(
        at,std::string_view("continuous.mp3").size(),"continuous.flac");
    return plan;
}
std::string header(std::string cueText,unsigned padding=1500,
                   bool native_type5=false) {
    std::string bytes="fLaC";
    bytes+=block(0,std::string(34,'\0'));
    bytes+=block(2,std::string("application metadata\0\1",22));
    bytes+=block(4,vorbis({"REPLAYGAIN_TRACK_GAIN=-7.23 dB",
                           "DATE=2026",
                           "CUESHEET="+std::move(cueText),
                           "CUSTOM=keep-this-unchanged"}));
    if (native_type5) bytes+=block(5,"binary CUE preservation");
    bytes+=block(1,std::string(padding,'\0'),true);
    return bytes;
}
void positive_preservation_and_noop() {
    const auto before=cue("Continuous Mix");
    const auto after=cue("Continuous Mix – Über 東京");
    const auto bytes=header(before.external_cue_utf8);
    const std::string audio="synthetic FLAC frames - no private audio";
    const auto edit=preview_precision_cue_flac_vorbis_patch(
        bytes,after.external_cue_utf8,PrecisionCueEmbeddedState::TextualPresent);
    CHECK(edit.state==PrecisionCueFlacPatchState::PreviewReady);
    CHECK(edit.metadata_bytes==bytes.size());
    CHECK(edit.postimage_metadata.size()==bytes.size());
    CHECK(edit.untouched_comment_count==3);
    CHECK(edit.untouched_block_count==2);
    CHECK(edit.source_sha256==precision_cue_sha256(bytes));
    CHECK(edit.postimage_sha256==precision_cue_sha256(edit.postimage_metadata));
    CHECK(edit.source_sha256!=edit.postimage_sha256);
    CHECK(edit.postimage_metadata.find("REPLAYGAIN_TRACK_GAIN=-7.23 dB") !=
          std::string::npos);
    CHECK(edit.postimage_metadata.find("CUSTOM=keep-this-unchanged") !=
          std::string::npos);
    CHECK(edit.postimage_metadata.find("application metadata") != std::string::npos);
    CHECK(edit.postimage_metadata.find(after.external_cue_utf8) != std::string::npos);
    CHECK((bytes+audio).substr(bytes.size())==
          (edit.postimage_metadata+audio).substr(edit.postimage_metadata.size()));
    const auto again=preview_precision_cue_flac_vorbis_patch(
        edit.postimage_metadata,after.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent);
    CHECK(again.state==PrecisionCueFlacPatchState::NoOp);
    CHECK(again.postimage_metadata.empty());
    CHECK(again.source_sha256==again.postimage_sha256);
    const auto shorten=preview_precision_cue_flac_vorbis_patch(
        edit.postimage_metadata,before.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent);
    CHECK(shorten.state==PrecisionCueFlacPatchState::PreviewReady);
    CHECK(shorten.postimage_metadata==bytes);
}
void negative_fail_closed_matrix() {
    using State=PrecisionCueFlacPatchState;
    const auto original=cue("Mix");
    const auto revised=cue("New Mix");
    const auto valid=header(original.external_cue_utf8);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        valid,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::NativeBinaryPresent).state==
        State::ReviewRequired);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        valid,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::MultipleOrConflicting).state==
        State::ReviewRequired);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        header(original.external_cue_utf8,1500,true),revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::ReviewRequired);
    auto duplicate="fLaC"+block(0,std::string(34,'\0'))+
        block(4,vorbis({"CUESHEET="+original.external_cue_utf8,
                        "CUESHEET="+original.external_cue_utf8}),true);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        duplicate,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::ReviewRequired);
    auto absent="fLaC"+block(0,std::string(34,'\0'))+
        block(4,vorbis({"ARTIST=Unrelated"}),true);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        absent,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::ReviewRequired);
    const auto noPadding="fLaC"+block(0,std::string(34,'\0'))+
        block(4,vorbis({"CUESHEET="+original.external_cue_utf8}),true);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        noPadding,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::ReviewRequired);
    const auto tiny=header(original.external_cue_utf8,4);
    CHECK(preview_precision_cue_flac_vorbis_patch(
        tiny,cue(std::string(1000,'L')).external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::ReviewRequired);
    auto wrong=cue("New Mix");
    const auto pos=wrong.external_cue_utf8.find("continuous.flac");
    CHECK(pos!=std::string::npos);
    wrong.external_cue_utf8.replace(pos,15,"unrelated.flac");
    CHECK(preview_precision_cue_flac_vorbis_patch(
        valid,wrong.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::ReviewRequired);
    auto invalid=valid;
    invalid[0]='X';
    CHECK(preview_precision_cue_flac_vorbis_patch(
        invalid,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::Rejected);
    invalid=valid;
    invalid[5]=static_cast<char>(0xff); // STREAMINFO block bad size.
    CHECK(preview_precision_cue_flac_vorbis_patch(
        invalid,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::Rejected);
    invalid=valid;
    invalid.back()='!'; // Non-zero padding.
    CHECK(preview_precision_cue_flac_vorbis_patch(
        invalid,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::Rejected);
    invalid=valid;
    invalid.resize(22); // Truncated STREAMINFO.
    CHECK(preview_precision_cue_flac_vorbis_patch(
        invalid,revised.external_cue_utf8,
        PrecisionCueEmbeddedState::TextualPresent).state==State::Rejected);
}
void bounded_file_prefix_readonly() {
    namespace fs=std::filesystem;
    std::random_device random;
    const auto dir=fs::canonical(fs::temp_directory_path()) /
        ("precision-cue-flac-fixture-"+std::to_string(random())+
         "-"+std::to_string(random()));
    CHECK(fs::create_directory(dir));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code e; fs::remove_all(path,e); }
    } cleanup{dir};
    const auto physical=dir/"synthetic.flac";
    const auto bytes=header(cue("Mix").external_cue_utf8);
    const std::string payload="not real audio";
    {
        std::ofstream out(physical,std::ios::binary);
        out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        out<<payload;
    }
    const auto mtime=fs::last_write_time(physical);
    const auto read=read_precision_cue_flac_metadata_prefix(physical);
    CHECK(read.has_value() && *read==bytes);
    CHECK(fs::last_write_time(physical)==mtime);
    {
        std::ifstream f(physical,std::ios::binary);
        const std::string stored((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        CHECK(stored==bytes+payload);
    }
    const auto bad=dir/"bad.flac";
    {
        std::ofstream out(bad,std::ios::binary);
        out<<"fLaC";
        out.write("\x80\xff\xff\xff",4);
    }
    CHECK(!read_precision_cue_flac_metadata_prefix(bad).has_value());
}
}
int main() {
    positive_preservation_and_noop();
    negative_fail_closed_matrix();
    bounded_file_prefix_readonly();
    std::puts("PASS: pure FLAC Vorbis CUESHEET postimages preserve ReplayGain, Type-5 and audio offsets");
    return 0;
}
