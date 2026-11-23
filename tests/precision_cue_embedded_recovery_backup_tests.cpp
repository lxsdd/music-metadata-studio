#include "djmeta/precision_cue_embedded_recovery_backup.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr,"Precision CUE recovery backup test failure %d: %s\n",\
        __LINE__,#cond); std::exit(1); } } while(false)

namespace {
namespace fs=std::filesystem;
struct Sandbox {
    fs::path directory;
    Sandbox() {
        std::random_device random;
        directory=fs::canonical(fs::temp_directory_path()) /
            ("precision-cue-backup-fixture-"+std::to_string(random())+
             "-"+std::to_string(random()));
        CHECK(fs::create_directory(directory));
    }
    ~Sandbox() {
        std::error_code ignored;
        fs::remove_all(directory,ignored);
    }
};
void save(const fs::path& filename,std::string_view bytes) {
    std::ofstream out(filename,std::ios::binary);
    CHECK(out.good());
    out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    CHECK(out.good());
}
std::string read_all(const fs::path& filename) {
    std::ifstream in(filename,std::ios::binary);
    CHECK(in.good());
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
std::size_t count_named(const Sandbox& area,std::string_view marker) {
    std::size_t n=0;
    for(const auto& entry:fs::directory_iterator(area.directory))
        if (entry.path().filename().string().starts_with(marker)) ++n;
    return n;
}
struct Source {
    fs::path filename;
    std::string prefix;
    std::string full_bytes;
    PrecisionCuePhysicalFullSnapshot evidence;
    fs::file_time_type original_time;
};
Source make_source(Sandbox& sandbox) {
    Source s;
    s.filename=sandbox.directory/"synthetic.flac";
    s.prefix="fLaC";
    s.prefix.append(4092,'M');
    s.prefix[22]='\0';
    s.full_bytes=s.prefix;
    const std::string audio(3u*65536u+241u,'A');
    s.full_bytes+=audio;
    save(s.filename,s.full_bytes);
    s.original_time=fs::last_write_time(s.filename);
    const auto captured=capture_precision_cue_physical_full_snapshot(
        s.filename,s.prefix);
    CHECK(captured.has_value());
    s.evidence=*captured;
    return s;
}
void success_and_explicit_permission() {
    using Status=PrecisionCueRecoveryBackupStatus;
    Sandbox area;
    const auto source=make_source(area);
    CHECK(prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix).status==Status::Rejected);
    CHECK(count_named(area,".precision-cue-backup-")==0);
    const auto result=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true);
    CHECK(result.status==Status::VerifiedBackupCreated);
    CHECK(!result.verified_backup_path.empty());
    CHECK(fs::exists(result.verified_backup_path));
    CHECK(result.verified_backup_path.parent_path()==area.directory);
    CHECK(result.verified_sha256==source.evidence.full_file_sha256);
    CHECK(result.bytes==source.full_bytes.size());
    CHECK(precision_cue_recovery_backup_matches_review(
        result.verified_backup_path,source.evidence,source.prefix));
    CHECK(read_all(result.verified_backup_path)==source.full_bytes);
    CHECK(read_all(source.filename)==source.full_bytes);
    CHECK(fs::last_write_time(source.filename)==source.original_time);
    CHECK(count_named(area,".precision-cue-backup-pending-")==0);
    CHECK(count_named(area,".precision-cue-backup-verified-")==1);
    // Two deliberate new-backup requests get two distinct targets; the
    // earlier backup is never overwritten or deleted by another operation.
    const auto second=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true);
    CHECK(second.status==Status::VerifiedBackupCreated);
    CHECK(result.verified_backup_path!=second.verified_backup_path);
    CHECK(read_all(result.verified_backup_path)==source.full_bytes);
    CHECK(count_named(area,".precision-cue-backup-verified-")==2);
    const auto unrelated=area.directory/"unrelated.bak";
    save(unrelated,source.full_bytes);
    CHECK(!precision_cue_recovery_backup_matches_review(
        unrelated,source.evidence,source.prefix));
}
void stale_source_and_bad_pending_are_fail_closed() {
    using Status=PrecisionCueRecoveryBackupStatus;
    using Checkpoint=PrecisionCueRecoveryBackupCheckpoint;
    Sandbox area;
    const auto source=make_source(area);
    const auto before=source.full_bytes;
    // Change the source without changing its file size or mtime, then attempt
    // to publish the previously reviewed backup. No verified backup appears.
    const auto stale=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true,
        [&](Checkpoint when,const fs::path&) {
            if (when!=Checkpoint::AfterPendingClone) return;
            std::fstream stream(source.filename,
                std::ios::in|std::ios::out|std::ios::binary);
            CHECK(stream.good());
            stream.seekp(static_cast<std::streamoff>(source.prefix.size()+51));
            stream.put('X');
            stream.close();
            fs::last_write_time(source.filename,source.original_time);
        });
    CHECK(stale.status==Status::SourceStale);
    CHECK(count_named(area,".precision-cue-backup-")==0);

    save(source.filename,before);
    fs::last_write_time(source.filename,source.original_time);
    CHECK(revalidate_precision_cue_physical_full_snapshot(
        source.evidence,source.prefix));

    // Corrupt the tentative backup during clone; the verified name is never
    // published and the incomplete clone is removed.
    const auto tampered=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true,
        [&](Checkpoint when,const fs::path& pending) {
            if (when!=Checkpoint::AfterPendingClone) return;
            std::fstream stream(pending,
                std::ios::in|std::ios::out|std::ios::binary);
            CHECK(stream.good());
            stream.seekp(static_cast<std::streamoff>(source.prefix.size()+71));
            stream.put('Z');
        });
    CHECK(tampered.status==Status::IoError);
    CHECK(count_named(area,".precision-cue-backup-")==0);

    const auto interrupted=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true,
        [&](Checkpoint when,const fs::path&) {
            if (when==Checkpoint::AfterPendingVerified)
                throw std::runtime_error("synthetic failure before publication");
        });
    CHECK(interrupted.status==Status::IoError);
    CHECK(count_named(area,".precision-cue-backup-")==0);
    CHECK(read_all(source.filename)==before);
}
void conflict_preserves_foreign_file() {
    using Status=PrecisionCueRecoveryBackupStatus;
    using Checkpoint=PrecisionCueRecoveryBackupCheckpoint;
    Sandbox area;
    const auto source=make_source(area);
    fs::path foreign;
    const std::string sentinel="existing backup collision must survive";
    const auto collision=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true,
        [&](Checkpoint when,const fs::path& pending) {
            if (when!=Checkpoint::AfterPendingVerified) return;
            foreign=precision_cue_recovery_backup_detail::
                backup_candidate_from_pending(pending);
            CHECK(!foreign.empty());
            save(foreign,sentinel);
        });
    CHECK(collision.status==Status::DestinationCollision);
    CHECK(read_all(foreign)==sentinel);
    CHECK(count_named(area,".precision-cue-backup-pending-")==0);
    CHECK(read_all(source.filename)==source.full_bytes);
}
void postpublish_fault_preserves_backup_for_review() {
    using Status=PrecisionCueRecoveryBackupStatus;
    using Checkpoint=PrecisionCueRecoveryBackupCheckpoint;
    Sandbox area;
    const auto source=make_source(area);
    fs::path published;
    const auto interrupted=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true,
        [&](Checkpoint when,const fs::path& backup) {
            if (when!=Checkpoint::AfterBackupPublished) return;
            published=backup;
            throw std::runtime_error("synthetic process interruption");
        });
    CHECK(interrupted.status==Status::IoError);
    CHECK(fs::exists(published));
    CHECK(precision_cue_recovery_backup_matches_review(
        published,source.evidence,source.prefix));
    CHECK(count_named(area,".precision-cue-backup-pending-")==0);
    CHECK(read_all(source.filename)==source.full_bytes);

    fs::path corrupted;
    const auto modified=prepare_precision_cue_recovery_backup(
        source.evidence,source.prefix,true,
        [&](Checkpoint when,const fs::path& backup) {
            if (when!=Checkpoint::AfterBackupPublished) return;
            corrupted=backup;
            std::fstream stream(backup,
                std::ios::in|std::ios::out|std::ios::binary);
            CHECK(stream.good());
            stream.seekp(static_cast<std::streamoff>(source.prefix.size()+17));
            stream.put('B');
        });
    CHECK(modified.status==Status::PublishedNeedsReview);
    CHECK(fs::exists(corrupted));
    CHECK(!precision_cue_recovery_backup_matches_review(
        corrupted,source.evidence,source.prefix));
    CHECK(precision_cue_recovery_backup_matches_review(
        published,source.evidence,source.prefix));
    CHECK(read_all(source.filename)==source.full_bytes);
}
void backup_requires_exact_snapshot() {
    using Status=PrecisionCueRecoveryBackupStatus;
    Sandbox area;
    const auto source=make_source(area);
    auto tampered=source.evidence;
    tampered.full_file_sha256[0]^=0x80u;
    CHECK(prepare_precision_cue_recovery_backup(
        tampered,source.prefix,true).status==Status::SourceStale);
    auto wrong=source.evidence;
    wrong.physical.file_id^=1u;
    CHECK(prepare_precision_cue_recovery_backup(
        wrong,source.prefix,true).status==Status::SourceStale);
    CHECK(prepare_precision_cue_recovery_backup(
        source.evidence,"WRONG",true).status==Status::SourceStale);
    CHECK(count_named(area,".precision-cue-backup-")==0);
}
}
int main() {
    success_and_explicit_permission();
    stale_source_and_bad_pending_are_fail_closed();
    conflict_preserves_foreign_file();
    postpublish_fault_preserves_backup_for_review();
    backup_requires_exact_snapshot();
    std::puts("PASS: create-only SHA-verified backup, stale/collision/fault retention");
    return 0;
}
