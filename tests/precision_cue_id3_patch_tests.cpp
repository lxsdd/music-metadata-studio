#include "djmeta/precision_cue_id3_file_preview.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include "djmeta/precision_cue.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"Precision CUE ID3 postimage failure line %d: %s\n", \
        __LINE__,#x); std::exit(1); } } while(false)

namespace {
void encode32(std::string& s, unsigned number, bool safe) {
    for (int shift=3; shift>=0; --shift) {
        const unsigned scale=(safe ? 7u : 8u)*static_cast<unsigned>(shift);
        s.push_back(static_cast<char>((number>>scale) & (safe ? 127u:255u)));
    }
}
std::string frame(std::string name, std::string payload, unsigned major) {
    std::string result=std::move(name);
    encode32(result, static_cast<unsigned>(payload.size()),major==4);
    result += std::string(2,'\0');
    result += payload;
    return result;
}
std::string tag(std::string frames, unsigned major, std::size_t space=2048) {
    CHECK(space >= frames.size() && space < 4u*1024u*1024u);
    std::string id3="ID3";
    id3.push_back(static_cast<char>(major));
    id3.push_back('\0');
    id3.push_back('\0');
    encode32(id3,static_cast<unsigned>(space),true);
    frames.resize(space,'\0');
    return id3+frames;
}
std::string txxx_utf8(std::string_view content, std::string name="CUESHEET") {
    std::string field(1,'\3');
    field+=std::move(name);
    field.push_back('\0');
    field.append(content);
    return field;
}
std::string txxx_latin1(std::string_view content) {
    std::string field(1,'\0');
    field+="CUESHEET";
    field.push_back('\0');
    field.append(content);
    return field;
}
std::string txxx_utf16_ascii(std::string_view content) {
    std::string field(1,'\1');
    field.push_back(static_cast<char>(255));
    field.push_back(static_cast<char>(254));
    for (const char c : std::string_view("CUESHEET")) {
        field.push_back(c);
        field.push_back('\0');
    }
    field += std::string(2,'\0');
    for (const char c : content) {
        CHECK(static_cast<unsigned char>(c)<128);
        field.push_back(c);
        field.push_back('\0');
    }
    return field;
}
PrecisionCuePlan synthetic(std::string title) {
    PrecisionCueDisc d;
    d.album_title=std::move(title);
    d.album_performer="DJ Example";
    d.audio_file_basename="mix.mp3";
    d.genre="Electronic";
    d.release_date="2026";
    d.tracks.push_back({"Opening (Mixed)","Artist One",91234});
    d.tracks.push_back({"Outro (Mixed)","Artist Two",89321});
    return make_precision_cue(d);
}
PrecisionCueId3Patch patch(std::string_view tag_bytes, std::string_view cue,
    PrecisionCueEmbeddedCrosscheckState crosscheck =
        PrecisionCueEmbeddedCrosscheckState::TextualAgreementReadOnly) {
    return preview_precision_cue_id3_txxx_patch(tag_bytes,cue,crosscheck);
}
void v24_patch_preserves_other_frames_and_full_audio() {
    const auto old=synthetic("Mix");
    const auto changed=synthetic("Mix Über 東京 2026");
    const auto priv=frame("PRIV",std::string("opaque\0binary\1",14),4);
    const auto rg=frame("TXXX",txxx_utf8("-8.123 dB","REPLAYGAIN_TRACK_GAIN"),4);
    const auto oldcue=frame("TXXX",txxx_utf8(old.external_cue_utf8),4);
    const auto cover=frame("APIC",std::string("\3image/jpeg\0",12)+"GIF",4);
    const std::string original=tag(priv+oldcue+rg+cover,4,4096);
    const std::string audio="synthetic audio bytes \0\xff\x80";
    const auto preview=patch(original,changed.external_cue_utf8);
    CHECK(preview.state==PrecisionCueId3PatchState::PreviewReady);
    CHECK(preview.postimage_tag.size()==original.size());
    CHECK(preview.tag_bytes==original.size());
    CHECK(preview.postimage_tag.substr(0,10)==original.substr(0,10));
    CHECK(preview.postimage_tag.find(priv)==10);
    CHECK(preview.postimage_tag.find(rg)!=std::string::npos);
    CHECK(preview.postimage_tag.find(cover)!=std::string::npos);
    CHECK(preview.postimage_tag.find(changed.external_cue_utf8)!=std::string::npos);
    CHECK(preview.source_sha256==precision_cue_sha256(original));
    CHECK(preview.postimage_sha256==precision_cue_sha256(preview.postimage_tag));
    CHECK(preview.source_sha256!=preview.postimage_sha256);
    CHECK(preview.untouched_frames==3);
    // No audio payload is owned by the planner; synthesised full image keeps
    // every audio byte in precisely the same offset.
    CHECK((original+audio).substr(original.size())==
          (preview.postimage_tag+audio).substr(preview.postimage_tag.size()));
    const auto repeat=patch(preview.postimage_tag,changed.external_cue_utf8);
    CHECK(repeat.state==PrecisionCueId3PatchState::NoOp);
    CHECK(repeat.postimage_tag.empty());
    CHECK(repeat.source_sha256==repeat.postimage_sha256);
    const auto unchanged=patch(original,old.external_cue_utf8);
    CHECK(unchanged.state==PrecisionCueId3PatchState::NoOp);
    CHECK(unchanged.postimage_tag.empty());
}
void v23_ascii_and_unicode() {
    const auto original=synthetic("Mix");
    const auto replacement=synthetic("Neue Ausgabe");
    const auto latin=tag(frame("TXXX",txxx_latin1(original.external_cue_utf8),3) +
                         frame("TIT2",std::string(1,'\0')+"Example",3),3,2048);
    CHECK(patch(latin,replacement.external_cue_utf8).state==
          PrecisionCueId3PatchState::PreviewReady);
    const auto non_ascii=synthetic("Große Übertragung");
    CHECK(patch(latin,non_ascii.external_cue_utf8).state==
          PrecisionCueId3PatchState::ReviewRequired);
    // ID3v2.3 Unicode textual TXXX uses BOM-tagged UTF16, not v2.4 UTF8.
    const auto wide=tag(frame("TXXX",txxx_utf16_ascii(original.external_cue_utf8),3),3,8192);
    const auto edit=patch(wide,non_ascii.external_cue_utf8);
    CHECK(edit.state==PrecisionCueId3PatchState::PreviewReady);
    CHECK(edit.postimage_tag.size()==wide.size());
    CHECK(patch(edit.postimage_tag,non_ascii.external_cue_utf8).state==
          PrecisionCueId3PatchState::NoOp);
}
void encoding_version_gate() {
    using State = PrecisionCueId3PatchState;
    const auto old = synthetic("Mix");
    const auto changed = synthetic("Mix New");
    const auto payload_utf8 = txxx_utf8(old.external_cue_utf8);
    // An ID3v2.3 TXXX frame with encoding 3 may be parseable UTF-8,
    // but it is not a valid v2.3 encoding. Reject edits AND no-op.
    const auto invalid_v23_utf8 = tag(frame("TXXX",payload_utf8,3),3,2048);
    CHECK(patch(invalid_v23_utf8,changed.external_cue_utf8).state==
          State::ReviewRequired);
    CHECK(patch(invalid_v23_utf8,old.external_cue_utf8).state==
          State::ReviewRequired);
    std::string utf16be(1,'\\2');
    for (const char c : std::string_view("CUESHEET")) {
        utf16be.push_back('\\0');
        utf16be.push_back(c);
    }
    utf16be += std::string(2,'\\0');
    for (const char c : old.external_cue_utf8) {
        CHECK(static_cast<unsigned char>(c)<128);
        utf16be.push_back('\\0');
        utf16be.push_back(c);
    }
    const auto invalid_v23_utf16be=tag(frame("TXXX",utf16be,3),3,8192);
    CHECK(patch(invalid_v23_utf16be,changed.external_cue_utf8).state==
          State::ReviewRequired);
    CHECK(patch(invalid_v23_utf16be,old.external_cue_utf8).state==
          State::ReviewRequired);
    // ID3v2.4 explicitly permits UTF-16BE (2); it must remain writable
    // in the *in-memory planner* and retain the original tag allocation.
    const auto valid_v24_utf16be=tag(frame("TXXX",utf16be,4),4,8192);
    const auto proposal=patch(valid_v24_utf16be,changed.external_cue_utf8);
    CHECK(proposal.state==State::PreviewReady);
    CHECK(proposal.postimage_tag.size()==valid_v24_utf16be.size());
    CHECK(patch(proposal.postimage_tag,changed.external_cue_utf8).state==
          State::NoOp);
}
void file_preview_is_bounded_and_read_only() {
    namespace fs=std::filesystem;
    std::random_device random;
    const auto where=fs::canonical(fs::temp_directory_path()) /
        ("precision-cue-id3-fixture-" + std::to_string(random()) +
         "-" + std::to_string(random()));
    // Never reuse or delete a pre-existing filesystem item.
    CHECK(fs::create_directory(where));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path,ec); }
    } cleanup{where};
    const auto mp3=where/"synthetic-metadata-only.mp3";
    const auto cue=synthetic("Mix");
    const auto bytes=tag(frame("TXXX",txxx_utf8(cue.external_cue_utf8),4),4,2048);
    {
        std::ofstream out(mp3,std::ios::binary);
        CHECK(out.good());
        out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        out << "not actual audio";
        CHECK(out.good());
    }
    const auto before=fs::last_write_time(mp3);
    const auto read=read_precision_cue_id3_tag_for_preview(mp3);
    CHECK(read.has_value() && *read==bytes);
    CHECK(fs::last_write_time(mp3)==before);
    const auto plan=patch(*read,synthetic("Mix New").external_cue_utf8);
    CHECK(plan.state==PrecisionCueId3PatchState::PreviewReady);
    CHECK(fs::file_size(mp3)==bytes.size()+std::string("not actual audio").size());
    {
        std::ifstream in(mp3,std::ios::binary);
        const std::string original((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
        CHECK(original==bytes+"not actual audio");
    }
    auto too_large=bytes;
    too_large[6]=static_cast<char>(0x80);
    const auto invalid=where/"invalid.mp3";
    {
        std::ofstream out(invalid,std::ios::binary);
        out.write(too_large.data(),static_cast<std::streamsize>(too_large.size()));
    }
    CHECK(!read_precision_cue_id3_tag_for_preview(invalid).has_value());
}
void reject_unsafe_and_ambiguous() {
    const auto before=synthetic("Mix");
    const auto after=synthetic("A Different Mix");
    const auto value=txxx_utf8(before.external_cue_utf8);
    const auto valid=tag(frame("TXXX",value,4),4,2048);
    using Cross=PrecisionCueEmbeddedCrosscheckState;
    using State=PrecisionCueId3PatchState;
    CHECK(patch(valid,after.external_cue_utf8,Cross::NoCueObserved).state==
          State::ReviewRequired);
    CHECK(patch(valid,after.external_cue_utf8,Cross::ReviewRequired).state==
          State::ReviewRequired);
    CHECK(patch(valid,after.external_cue_utf8,Cross::ProtectedNativeBinary).state==
          State::ReviewRequired);
    CHECK(patch(tag(frame("TXXX",value,4)+frame("TXXX",value,4),4,4096),
                after.external_cue_utf8).state==State::ReviewRequired);
    CHECK(patch(tag(frame("TXXX",txxx_utf8("not a cue","ALBUM"),4),4),
                after.external_cue_utf8).state==State::ReviewRequired);
    CHECK(patch(valid,std::string("random garbage")).state==State::Rejected);
    auto different=after;
    const auto old=std::string("mix.mp3");
    const auto replacement=std::string("other.mp3");
    const auto at=different.external_cue_utf8.find(old);
    CHECK(at!=std::string::npos);
    different.external_cue_utf8.replace(at,old.size(),replacement);
    CHECK(patch(valid,different.external_cue_utf8).state==State::ReviewRequired);
    auto unsupported=valid;
    unsupported[5]=static_cast<char>(0x80);
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
    unsupported=valid;
    unsupported[6]=static_cast<char>(0x80);
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
    unsupported=valid;
    unsupported[3]=static_cast<char>(2);
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
    unsupported=valid;
    unsupported.resize(20);
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
    unsupported=valid;
    const auto offset=unsupported.find("TXXX");
    CHECK(offset!=std::string::npos);
    unsupported[offset+8]=static_cast<char>(0x20);
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
    unsupported=valid;
    unsupported[offset+4]=static_cast<char>(0x80);
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
    const auto big=synthetic(std::string(750,'x'));
    CHECK(patch(tag(frame("TXXX",value,4),4,
                    frame("TXXX",value,4).size()+4),big.external_cue_utf8).state==
          State::ReviewRequired);
    // The postimage's padding must be all zeroes, no hidden frames or blobs.
    unsupported=valid;
    unsupported.back()='!';
    CHECK(patch(unsupported,after.external_cue_utf8).state==State::Rejected);
}
}
int main() {
    v24_patch_preserves_other_frames_and_full_audio();
    v23_ascii_and_unicode();
    encoding_version_gate();
    reject_unsafe_and_ambiguous();
    file_preview_is_bounded_and_read_only();
    std::puts("PASS: in-memory ID3v2.3/v2.4 CUESHEET postimages preserve all other frames and audio offsets");
    return 0;
}
