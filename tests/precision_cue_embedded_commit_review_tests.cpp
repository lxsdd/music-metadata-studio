#include "djmeta/precision_cue_embedded_commit_review.h"
#include "djmeta/precision_cue.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"Precision CUE commit-review test failed line %d: %s\n",\
        __LINE__,#x); std::exit(1); } } while(false)

namespace {
namespace fs=std::filesystem;
struct Sandbox {
    fs::path dir;
    Sandbox() {
        std::random_device random;
        dir=fs::canonical(fs::temp_directory_path()) /
            ("precision-cue-commit-review-"+std::to_string(random())+
             "-"+std::to_string(random()));
        CHECK(fs::create_directory(dir));
    }
    ~Sandbox() { std::error_code ec; fs::remove_all(dir,ec); }
};
void save(const fs::path& target,std::string_view bytes) {
    std::ofstream out(target,std::ios::binary);
    CHECK(out.good());
    out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    CHECK(out.good());
}
PrecisionCuePlan cue(std::string title,bool flac=false) {
    PrecisionCueDisc d;
    d.audio_file_basename="mix.mp3";
    d.album_title=std::move(title);
    d.album_performer="Synthetic DJ";
    d.genre="Trance";
    d.release_date="2026";
    d.tracks.push_back({"Opening","A",120000});
    d.tracks.push_back({"Finale","B",127000});
    auto result=make_precision_cue(d);
    if (flac) {
        const auto pos=result.external_cue_utf8.find("mix.mp3");
        CHECK(pos!=std::string::npos);
        result.external_cue_utf8.replace(pos,7,"mix.flac");
    }
    return result;
}
void little(std::string& dest,unsigned value) {
    for (unsigned i=0;i<4;++i)
        dest.push_back(static_cast<char>((value>>(i*8))&255));
}
void synchsafe(std::string& dest,unsigned value) {
    for(int i=3;i>=0;--i)
        dest.push_back(static_cast<char>((value>>(i*7))&127));
}
std::string mp3_prefix(std::string_view cue_text) {
    std::string value(1,'\3');
    value+="CUESHEET";
    value.push_back('\0');
    value.append(cue_text);
    std::string frames="TXXX";
    synchsafe(frames,static_cast<unsigned>(value.size()));
    frames+=std::string(2,'\0')+value;
    const unsigned reserve=4096;
    CHECK(frames.size()<reserve);
    frames.resize(reserve,'\0');
    std::string out="ID3";
    out.push_back('\4');
    out+=std::string(2,'\0');
    synchsafe(out,reserve);
    return out+frames;
}
std::string flac_block(unsigned kind,const std::string& payload,bool final=false) {
    const auto n=static_cast<unsigned>(payload.size());
    std::string s(1,static_cast<char>(kind|(final?128:0)));
    s.push_back(static_cast<char>((n>>16)&255));
    s.push_back(static_cast<char>((n>>8)&255));
    s.push_back(static_cast<char>(n&255));
    return s+payload;
}
std::string flac_prefix(std::string_view cue_text) {
    std::string values;
    little(values,4);
    values+="Test";
    little(values,2);
    const std::string rg="REPLAYGAIN_TRACK_GAIN=-5.12 dB";
    little(values,static_cast<unsigned>(rg.size()));
    values+=rg;
    const std::string t="CUESHEET="+std::string(cue_text);
    little(values,static_cast<unsigned>(t.size()));
    values+=t;
    return "fLaC"+flac_block(0,std::string(34,'\0'))+
        flac_block(4,values)+flac_block(1,std::string(4096,'\0'),true);
}
void one_carrier(bool flac) {
    using BackupStatus=PrecisionCueRecoveryBackupStatus;
    using StageStatus=PrecisionCueEmbeddedStageStatus;
    using Verdict=PrecisionCueEmbeddedCommitReviewState;
    Sandbox area;
    const auto original=cue("First",flac);
    const auto modified=cue("Revised Mix",flac);
    const auto prefix=flac ? flac_prefix(original.external_cue_utf8) :
                             mp3_prefix(original.external_cue_utf8);
    const auto physical=area.dir/(flac?"synthetic.flac":"synthetic.mp3");
    const std::string unchanged_audio(2u*65536u+129u,'A');
    const auto bytes=prefix+unchanged_audio;
    save(physical,bytes);
    const auto mtime=fs::last_write_time(physical);
    const auto snapshot=capture_precision_cue_physical_full_snapshot(physical,prefix);
    CHECK(snapshot.has_value());
    const PrecisionCueEmbeddedReviewedPatch patch=flac ?
        PrecisionCueEmbeddedReviewedPatch{
            preview_precision_cue_flac_vorbis_patch(
                prefix,modified.external_cue_utf8,
                PrecisionCueEmbeddedState::TextualPresent)} :
        PrecisionCueEmbeddedReviewedPatch{
            preview_precision_cue_id3_txxx_patch(
                prefix,modified.external_cue_utf8,
                PrecisionCueEmbeddedCrosscheckState::TextualAgreementReadOnly)};
    const auto dry=verify_precision_cue_embedded_staged_candidate(
        *snapshot,prefix,patch,true);
    CHECK(dry.status==StageStatus::VerifiedAndCleaned);
    const auto backup=prepare_precision_cue_recovery_backup(
        *snapshot,prefix,true);
    CHECK(backup.status==BackupStatus::VerifiedBackupCreated);
    CHECK(fs::exists(backup.verified_backup_path));
    auto review=review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,dry,backup.verified_backup_path,true);
    CHECK(review.state==Verdict::EvidenceCoherentHostWriteNotAvailable);
    CHECK(review.expected_candidate_sha256==dry.staged_full_sha256);
    CHECK(review.expected_file_length==bytes.size());
    CHECK(fs::file_size(physical)==bytes.size());
    CHECK(fs::last_write_time(physical)==mtime);

    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,dry,backup.verified_backup_path,false).state==
        Verdict::Rejected);
    auto forged_stage=dry;
    forged_stage.staged_full_sha256[1]^=0x7fu;
    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,forged_stage,backup.verified_backup_path,true).state==
        Verdict::NeedsReview);
    forged_stage=dry;
    forged_stage.total_bytes++;
    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,forged_stage,backup.verified_backup_path,true).state==
        Verdict::NeedsReview);
    forged_stage=dry;
    forged_stage.status=StageStatus::IoError;
    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,forged_stage,backup.verified_backup_path,true).state==
        Verdict::NeedsReview);

    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,dry,area.dir/"no-backup.bak",true).state==
        Verdict::NeedsReview);
    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,dry,physical,true).state==Verdict::NeedsReview);
    const auto original_sha=snapshot->full_file_sha256;
    auto tampered=*snapshot;
    tampered.full_file_sha256[0]^=0x01;
    CHECK(review_precision_cue_embedded_commit(
        tampered,prefix,patch,dry,backup.verified_backup_path,true).state==
        Verdict::NeedsReview);
    CHECK(snapshot->full_file_sha256==original_sha);

    // A same-size/same-mtime audio change must invalidate the entire review.
    {
        std::fstream file(physical,std::ios::binary|std::ios::in|std::ios::out);
        CHECK(file.good());
        file.seekp(static_cast<std::streamoff>(prefix.size()+30));
        file.put('X');
    }
    fs::last_write_time(physical,mtime);
    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,dry,backup.verified_backup_path,true).state==
        Verdict::NeedsReview);
    save(physical,bytes);
    fs::last_write_time(physical,mtime);
    CHECK(revalidate_precision_cue_physical_full_snapshot(*snapshot,prefix));

    // Backup corruption after verification must also revoke readiness.
    {
        std::fstream file(backup.verified_backup_path,
            std::ios::binary|std::ios::in|std::ios::out);
        CHECK(file.good());
        file.seekp(static_cast<std::streamoff>(prefix.size()+32));
        file.put('Y');
    }
    CHECK(review_precision_cue_embedded_commit(
        *snapshot,prefix,patch,dry,backup.verified_backup_path,true).state==
        Verdict::NeedsReview);
    CHECK(fs::file_size(physical)==bytes.size());
}
} // namespace
int main() {
    one_carrier(false);
    one_carrier(true);
    std::puts("PASS: embedded full-image SHA + verified backup gates remain host-write-disabled");
    return 0;
}
