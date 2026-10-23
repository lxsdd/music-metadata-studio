#pragma once
// Test-only real filesystem CUE transaction; never linked into foobar host.
// A host writer needs independent OS identity, locking and durability gates.
#include "djmeta/cue_apply_preflight.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
namespace djmeta::online::fixture {
enum class CueFixtureWriteStatus { Refused, Noop, Committed, Restored };
struct CueFixtureWriteResult {
    CueFixtureWriteStatus status=CueFixtureWriteStatus::Refused;
    std::string reason;
};
inline bool write_bytes(const std::filesystem::path& file,
                        std::string_view bytes) {
    std::ofstream f(file,std::ios::binary|std::ios::trunc);
    if(!f) return false;
    f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    f.flush();
    return static_cast<bool>(f);
}
inline bool read_bytes(const std::filesystem::path& file,
                       std::string& content) {
    std::ifstream f(file,std::ios::binary);
    if(!f) return false;
    content.assign(std::istreambuf_iterator<char>(f),
                   std::istreambuf_iterator<char>());
    // istreambuf_iterator reaches EOF without necessarily setting eofbit.
    return !f.bad();
}
// Never accept arbitrary target paths: caller must own an isolated empty test
// sandbox. This is deliberately separate from production host code.
inline CueFixtureWriteResult commit_fixture_cue(
    const std::filesystem::path& sandbox,
    const std::filesystem::path& file,
    const CueApplyPreflight& verified,
    std::string_view reviewed_source_bytes,
    bool inject_failure_after_backup=false) {
    namespace fs=std::filesystem;
    const auto reject=[](const char* why){
        return CueFixtureWriteResult{CueFixtureWriteStatus::Refused,why};
    };
    if(verified.readiness==CueApplyReadiness::Blocked)
        return reject("FIXTURE_PREFLIGHT_BLOCKED");
    if(verified.readiness==CueApplyReadiness::ExactNoop)
        return {CueFixtureWriteStatus::Noop,{}};
    if(verified.readiness!=CueApplyReadiness::DetachedCandidate ||
       !verified.postimage.changed ||
       verified.postimage.bytes==reviewed_source_bytes)
        return reject("FIXTURE_POSTIMAGE_INVALID");
    std::error_code ec;
    const auto root=fs::canonical(sandbox,ec);
    if(ec || !fs::is_directory(root))return reject("FIXTURE_ROOT_INVALID");
    const auto parent=fs::canonical(file.parent_path(),ec);
    if(ec || parent!=root || file.extension()!=".cue" ||
       fs::is_symlink(file,ec) || ec ||
       !fs::is_regular_file(file,ec) || ec)
        return reject("FIXTURE_SOURCE_NOT_ISOLATED");
    const auto stage=root/".djmeta-cue-fixture-stage";
    const auto backup=root/".djmeta-cue-fixture-backup";
    if(fs::exists(stage,ec) || fs::exists(backup,ec) || ec)
        return reject("FIXTURE_TRANSACTION_COLLISION");
    std::string original;
    if(!read_bytes(file,original) || original!=reviewed_source_bytes ||
       cue_carrier_content_fingerprint(original)!=
          cue_carrier_content_fingerprint(reviewed_source_bytes))
        return reject("FIXTURE_ORIGINAL_CHANGED");
    if(cue_carrier_content_fingerprint(verified.postimage.bytes)==
       cue_carrier_content_fingerprint(original))
        return reject("FIXTURE_CHANGED_FLAG_CONTRADICTS_BYTES");
    if(!write_bytes(stage,verified.postimage.bytes)) {
        fs::remove(stage,ec);
        return reject("FIXTURE_STAGE_WRITE_FAILED");
    }
    std::string staged;
    if(!read_bytes(stage,staged) || staged!=verified.postimage.bytes) {
        fs::remove(stage,ec);
        return reject("FIXTURE_STAGE_VERIFY_FAILED");
    }
    fs::rename(file,backup,ec);
    if(ec) {
        fs::remove(stage,ec);
        return reject("FIXTURE_BACKUP_RENAME_FAILED");
    }
    if(inject_failure_after_backup) {
        ec=std::make_error_code(std::errc::io_error);
    } else {
        fs::rename(stage,file,ec);
    }
    if(ec) {
        std::error_code restore_error;
        fs::rename(backup,file,restore_error);
        // Cleanup must not overwrite the result of the critical restore.
        std::error_code cleanup_error;
        fs::remove(stage,cleanup_error);
        if(restore_error)
            return {CueFixtureWriteStatus::Refused,
                    "FIXTURE_RESTORE_FAILED_BACKUP_RETAINED"};
        std::string restored_bytes;
        if(!read_bytes(file,restored_bytes) || restored_bytes!=original)
            return {CueFixtureWriteStatus::Refused,
                    "FIXTURE_RESTORE_BYTE_VERIFICATION_FAILED"};
        if(cleanup_error)
            return {CueFixtureWriteStatus::Refused,
                    "FIXTURE_RESTORE_OK_STAGE_CLEANUP_FAILED"};
        return {CueFixtureWriteStatus::Restored,
                "FIXTURE_FAILED_ORIGINAL_RESTORED"};
    }
    std::string final_bytes;
    if(!read_bytes(file,final_bytes) ||
       final_bytes!=verified.postimage.bytes) {
        // Do not delete backup if final verification failed.
        return {CueFixtureWriteStatus::Refused,
                "FIXTURE_VERIFY_FAILED_BACKUP_RETAINED"};
    }
    fs::remove(backup,ec);
    if(ec)return {CueFixtureWriteStatus::Refused,
                  "FIXTURE_BACKUP_CLEANUP_FAILED"};
    return {CueFixtureWriteStatus::Committed,{}};
}
} // namespace djmeta::online::fixture
