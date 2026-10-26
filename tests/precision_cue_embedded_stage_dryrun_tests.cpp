#include "djmeta/precision_cue_embedded_stage_dryrun.h"
#include "djmeta/precision_cue.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"Embedded dry-run verification failed line %d: %s\n",\
        __LINE__,#x); std::exit(1); } } while(false)

namespace {
namespace fs=std::filesystem;
struct Sandbox {
    fs::path path;
    Sandbox() {
        std::random_device random;
        path=fs::canonical(fs::temp_directory_path()) /
            ("precision-cue-stage-fixture-"+std::to_string(random())+
             "-"+std::to_string(random()));
        CHECK(fs::create_directory(path));
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(path,ec);
    }
};
void save(const fs::path& path,std::string_view bytes) {
    std::ofstream output(path,std::ios::binary);
    CHECK(output.good());
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    CHECK(output.good());
}
std::string readall(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void le32(std::string& out,unsigned n) {
    for (unsigned i=0;i<4;++i)
        out.push_back(static_cast<char>((n>>(i*8))&255));
}
void sync32(std::string& out,unsigned n) {
    for (int i=3;i>=0;--i)
        out.push_back(static_cast<char>((n>>(7*i))&127));
}
std::string id3(std::string_view cue) {
    std::string value(1,'\3');
    value+="CUESHEET";
    value.push_back('\0');
    value.append(cue);
    std::string frame="TXXX";
    sync32(frame,static_cast<unsigned>(value.size()));
    frame+=std::string(2,'\0');
    frame+=value;
    const unsigned allocation=4096;
    CHECK(frame.size()<allocation);
    std::string header="ID3";
    header.push_back('\4');
    header+=std::string(2,'\0');
    sync32(header,allocation);
    frame.resize(allocation,'\0');
    return header+frame;
}
std::string block(unsigned type,std::string_view payload,bool last=false) {
    const auto n=static_cast<unsigned>(payload.size());
    std::string header(1,static_cast<char>(type|(last?128u:0u)));
    header.push_back(static_cast<char>((n>>16)&255));
    header.push_back(static_cast<char>((n>>8)&255));
    header.push_back(static_cast<char>(n&255));
    header.append(payload);
    return header;
}
std::string flac(std::string_view cue) {
    std::string vorbis;
    le32(vorbis,5);
    vorbis+="Dummy";
    le32(vorbis,2);
    const std::string replay="REPLAYGAIN_TRACK_GAIN=-5.23 dB";
    le32(vorbis,static_cast<unsigned>(replay.size()));
    vorbis+=replay;
    const std::string field="CUESHEET="+std::string(cue);
    le32(vorbis,static_cast<unsigned>(field.size()));
    vorbis+=field;
    return "fLaC"+block(0,std::string(34,'\0'))+
        block(4,vorbis)+block(1,std::string(4096,'\0'),true);
}
PrecisionCuePlan synthetic(std::string title, bool flac_carrier=false) {
    PrecisionCueDisc disc;
    disc.audio_file_basename="mix.mp3";
    disc.album_title=std::move(title);
    disc.album_performer="DJ Synthetic";
    disc.release_date="2026";
    disc.genre="Electronic";
    disc.tracks.push_back({"Part 1","Synthetic Artist",120000});
    disc.tracks.push_back({"Part 2","Synthetic Artist",128000});
    auto plan=make_precision_cue(disc);
    if (flac_carrier) {
        const auto at=plan.external_cue_utf8.find("mix.mp3");
        CHECK(at!=std::string::npos);
        plan.external_cue_utf8.replace(at,7,"mix.flac");
    }
    return plan;
}
std::size_t stages(const Sandbox& sandbox) {
    std::size_t total=0;
    for(const auto& file:fs::directory_iterator(sandbox.path))
        if (file.path().filename().string().starts_with(".precision-cue-stage-"))
            ++total;
    return total;
}
void test_mp3_dryrun_stale_rollback() {
    using Status=PrecisionCueEmbeddedStageStatus;
    using Checkpoint=PrecisionCueEmbeddedStageCheckpoint;
    Sandbox sandbox;
    const auto physical=fs::canonical(sandbox.path)/"synthetic.mp3";
    const auto first=synthetic("Before");
    const auto second=synthetic("After Mixed");
    const auto prefix=id3(first.external_cue_utf8);
    const std::string payload(3u*65536u+139u,'s'); // no audio content.
    const auto before=prefix+payload;
    save(physical,before);
    const auto original_time=fs::last_write_time(physical);
    const auto review=capture_precision_cue_physical_full_snapshot(physical,prefix);
    CHECK(review.has_value());
    const auto patch=preview_precision_cue_id3_txxx_patch(
        prefix,second.external_cue_utf8,
        PrecisionCueEmbeddedCrosscheckState::TextualAgreementReadOnly);
    CHECK(patch.state==PrecisionCueId3PatchState::PreviewReady);
    const PrecisionCueEmbeddedReviewedPatch variant=patch;
    CHECK(verify_precision_cue_embedded_staged_candidate(
        *review,prefix,variant).status==Status::Rejected);
    CHECK(stages(sandbox)==0);

    const auto result=verify_precision_cue_embedded_staged_candidate(
        *review,prefix,variant,true);
    CHECK(result.status==Status::VerifiedAndCleaned);
    CHECK(result.total_bytes==before.size());
    CHECK(result.staged_full_sha256==
          precision_cue_sha256(patch.postimage_tag+payload));
    CHECK(stages(sandbox)==0);
    CHECK(readall(physical)==before);
    CHECK(fs::last_write_time(physical)==original_time);

    const auto noop=preview_precision_cue_id3_txxx_patch(
        prefix,first.external_cue_utf8,
        PrecisionCueEmbeddedCrosscheckState::TextualAgreementReadOnly);
    CHECK(noop.state==PrecisionCueId3PatchState::NoOp);
    CHECK(verify_precision_cue_embedded_staged_candidate(
        *review,prefix,PrecisionCueEmbeddedReviewedPatch{noop},true).status==
        Status::NoOp);
    CHECK(stages(sandbox)==0);

    // Fault between copy and tag patch: source size/time unchanged but its
    // audio bytes differ. No candidate is published and temp is removed.
    const auto stale=verify_precision_cue_embedded_staged_candidate(
        *review,prefix,variant,true,[&](Checkpoint stage,const fs::path&) {
            if (stage!=Checkpoint::AfterClone) return;
            std::fstream modified(physical,std::ios::in|std::ios::out|std::ios::binary);
            CHECK(modified.good());
            modified.seekp(static_cast<std::streamoff>(prefix.size()+100));
            modified.put('X');
            modified.close();
            fs::last_write_time(physical,original_time);
        });
    CHECK(stale.status==Status::ReviewRequired);
    CHECK(stages(sandbox)==0);
    save(physical,before);
    fs::last_write_time(physical,original_time);
    CHECK(revalidate_precision_cue_physical_full_snapshot(*review,prefix));

    const auto injected=verify_precision_cue_embedded_staged_candidate(
        *review,prefix,variant,true,[&](Checkpoint stage,const fs::path&) {
            if (stage==Checkpoint::AfterPatch)
                throw std::runtime_error("synthetic interruption after stage patch");
        });
    CHECK(injected.status==Status::IoError);
    CHECK(stages(sandbox)==0);
    CHECK(readall(physical)==before);

    const auto corrupted=verify_precision_cue_embedded_staged_candidate(
        *review,prefix,variant,true,[&](Checkpoint stage,const fs::path& path) {
            if (stage==Checkpoint::AfterPatch) {
                std::fstream output(path,std::ios::in|std::ios::out|std::ios::binary);
                CHECK(output.good());
                output.seekp(static_cast<std::streamoff>(prefix.size()+200));
                output.put('X');
            }
        });
    CHECK(corrupted.status==Status::ReviewRequired);
    CHECK(stages(sandbox)==0);

    const auto altered_after_verify=verify_precision_cue_embedded_staged_candidate(
        *review,prefix,variant,true,[&](Checkpoint stage,const fs::path& path) {
            if (stage==Checkpoint::AfterIndependentVerification) {
                std::fstream output(path,std::ios::in|std::ios::out|std::ios::binary);
                CHECK(output.good());
                output.seekp(200);
                output.put('X');
            }
        });
    CHECK(altered_after_verify.status==Status::ReviewRequired);
    CHECK(stages(sandbox)==0);
    CHECK(readall(physical)==before);
}
void test_exclusive_stage_creation_never_overwrites() {
    Sandbox sandbox;
    const auto source=fs::canonical(sandbox.path)/"synthetic.mp3";
    const auto sentinel=sandbox.path/".precision-cue-stage-existing.tmp";
    save(source,"synthetic audio bytes");
    save(sentinel,"SENTINEL MUST NEVER CHANGE");
    precision_cue_embedded_stage_detail::ScopedTemporaryFile owned(sentinel);
    CHECK(!precision_cue_embedded_stage_detail::clone_to_owned_new_stage(
        source,owned,fs::file_size(source)));
    CHECK(!owned.owned);
    CHECK(readall(sentinel)=="SENTINEL MUST NEVER CHANGE");
    CHECK(readall(source)=="synthetic audio bytes");
    // A failed attempt must neither acquire ownership nor delete the
    // preexisting file; its caller is responsible only for owned stages.
}
void test_flac_dryrun_preserves_audio() {
    using Status=PrecisionCueEmbeddedStageStatus;
    Sandbox sandbox;
    const auto physical=fs::canonical(sandbox.path)/"synthetic.flac";
    const auto old=synthetic("Before",true);
    const auto revised=synthetic("After",true);
    const auto prefix=flac(old.external_cue_utf8);
    const std::string payload(2u*65536u+233u,'F'); // not FLAC audio.
    const auto original=prefix+payload;
    save(physical,original);
    const auto source=capture_precision_cue_physical_full_snapshot(physical,prefix);
    CHECK(source.has_value());
    const auto patch=preview_precision_cue_flac_vorbis_patch(
        prefix,revised.external_cue_utf8,PrecisionCueEmbeddedState::TextualPresent);
    CHECK(patch.state==PrecisionCueFlacPatchState::PreviewReady);
    const auto result=verify_precision_cue_embedded_staged_candidate(
        *source,prefix,PrecisionCueEmbeddedReviewedPatch{patch},true);
    CHECK(result.status==Status::VerifiedAndCleaned);
    CHECK(result.staged_full_sha256==
          precision_cue_sha256(patch.postimage_metadata+payload));
    CHECK(stages(sandbox)==0);
    CHECK(readall(physical)==original);

    // Cross-carrier proposals cannot qualify even if their SHA is correct.
    const auto id3cue=synthetic("Other");
    const auto id3prefix=id3(id3cue.external_cue_utf8);
    const auto id3patch=preview_precision_cue_id3_txxx_patch(
        id3prefix,synthetic("New").external_cue_utf8,
        PrecisionCueEmbeddedCrosscheckState::TextualAgreementReadOnly);
    CHECK(id3patch.state==PrecisionCueId3PatchState::PreviewReady);
    CHECK(verify_precision_cue_embedded_staged_candidate(
        *source,prefix,PrecisionCueEmbeddedReviewedPatch{id3patch},true).status==
        Status::ReviewRequired);
    CHECK(stages(sandbox)==0);
}
}
int main() {
    test_mp3_dryrun_stale_rollback();
    test_flac_dryrun_preserves_audio();
    test_exclusive_stage_creation_never_overwrites();
    std::puts("PASS: embedded dry-run clones preserve suffix bytes, reject stale sources, clean all stages");
    return 0;
}
