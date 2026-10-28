#include "external_cue_transaction_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace djmeta::online;
namespace fs=std::filesystem;

static void check(bool condition,const char* why) {
    if (!condition) throw std::runtime_error(why);
}
static void write_file(const fs::path& path,const std::string& bytes) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    check(static_cast<bool>(out),"create test CUE");
    out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    out.flush();
    check(static_cast<bool>(out),"write test CUE");
}
static std::string read_file(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    check(static_cast<bool>(in),"open test CUE");
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}
static std::string physical_key(const fs::path& path) {
    const HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    check(file!=INVALID_HANDLE_VALUE,"read native CUE identity");
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid=GetFileInformationByHandle(file,&info)!=FALSE;
    CloseHandle(file);
    check(valid,"native CUE identity available");
    const std::uint64_t index=
        (static_cast<std::uint64_t>(info.nFileIndexHigh)<<32)|
        static_cast<std::uint64_t>(info.nFileIndexLow);
    return "win-file-v1:"+std::to_string(info.dwVolumeSerialNumber)+":"+
        std::to_string(index);
}
static CueApplyPreflight preflight(const std::string& source,
                                  const std::string& target) {
    CueApplyPreflight result;
    result.readiness=source==target
        ? CueApplyReadiness::ExactNoop
        : CueApplyReadiness::DetachedCandidate;
    result.postimage.eligible=true;
    result.postimage.changed=source!=target;
    result.postimage.bytes=target;
    return result;
}
int main() {
    fs::path root;
    try {
        const auto base=fs::temp_directory_path();
        const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code ec;
        for(unsigned attempt=0;attempt<128;++attempt) {
            const auto candidate=base/("mms-cue-win-"+std::to_string(nonce)+
                                       "-"+std::to_string(attempt));
            if(fs::create_directory(candidate,ec)) {
                root=candidate;
                break;
            }
            check(!ec,"create isolated CUE directory");
        }
        check(!root.empty(),"unique CUE sandbox unavailable");
        const auto path=root/"album.cue";
        const std::string original=
            "PERFORMER \"Synthetic Artist\"\r\n"
            "TITLE \"Original Title\"\r\n"
            "FILE \"audio.wav\" WAVE\r\n"
            "  TRACK 01 AUDIO\r\n"
            "    TITLE \"Original Title\"\r\n"
            "    INDEX 01 00:00:00\r\n";
        const std::string updated=
            "PERFORMER \"Synthetic Artist\"\r\n"
            "TITLE \"Reviewed Title\"\r\n"
            "FILE \"audio.wav\" WAVE\r\n"
            "  TRACK 01 AUDIO\r\n"
            "    TITLE \"Original Title\"\r\n"
            "    INDEX 01 00:00:00\r\n";
        write_file(path,original);
        const auto key=physical_key(path);
        const auto mtime=fs::last_write_time(path);
        const auto noop=commit_preflighted_external_cue_windows(
            path.wstring(),key,original,preflight(original,original));
        check(noop.status==ExternalCueCommitStatus::ExactNoop,
              "exact unchanged CUE must not write");
        check(read_file(path)==original && fs::last_write_time(path)==mtime,
              "no-op changed source bytes or timestamp");
        const auto wrong_id=commit_preflighted_external_cue_windows(
            path.wstring(),"win-file-v1:wrong",original,
            preflight(original,updated));
        check(wrong_id.status==ExternalCueCommitStatus::Refused &&
              read_file(path)==original,"wrong native ID was accepted");
        const auto stale=commit_preflighted_external_cue_windows(
            path.wstring(),key,"stale source bytes",preflight(original,updated));
        check(stale.status==ExternalCueCommitStatus::Refused &&
              read_file(path)==original,"stale original bytes were accepted");
        const auto non_cue=commit_preflighted_external_cue_windows(
            (root/"album.txt").wstring(),key,original,
            preflight(original,updated));
        check(non_cue.status==ExternalCueCommitStatus::Refused,
              "non-CUE path was accepted");
        check(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,
              "set read-only test attribute");
        const auto readonly=commit_preflighted_external_cue_windows(
            path.wstring(),key,original,preflight(original,updated));
        check(readonly.status==ExternalCueCommitStatus::Refused &&
              read_file(path)==original,"read-only source was mutated");
        check(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL)!=FALSE,
              "restore test attribute");
        const auto saved=commit_preflighted_external_cue_windows(
            path.wstring(),key,original,preflight(original,updated));
        check(saved.status==ExternalCueCommitStatus::Committed,
              "atomic CUE replace/readback failed");
        check(read_file(path)==updated,"committed CUE bytes differ");
        std::size_t files=0;
        for(const auto& item:fs::directory_iterator(root)) {
            check(item.path()==path,"unexpected staging/backup artifact");
            ++files;
        }
        check(files==1,"CUE backup/stage cleanup failed");
        const auto replay=commit_preflighted_external_cue_windows(
            path.wstring(),key,original,preflight(original,updated));
        check(replay.status==ExternalCueCommitStatus::Refused,
              "replayed old physical identity accepted");
        std::cout<<"PASS: native Windows external-CUE exact no-op, identity,"
                    " stale, readonly and transactional replace\n";
        fs::remove_all(root,ec);
        check(!ec,"cleanup isolated CUE sandbox");
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        if(!root.empty()) {
            std::error_code ec;
            // Only our generated unique sandbox is ever eligible for cleanup.
            fs::remove_all(root,ec);
        }
        return 1;
    }
}
