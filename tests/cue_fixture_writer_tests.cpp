#include "djmeta/cue_fixture_writer.h"
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
using namespace djmeta::online;
using namespace djmeta::online::fixture;
static void check(bool ok,const char* s) {
 if(!ok){std::cerr<<"FAIL: "<<s<<"\n";std::exit(1);}
}
int main(){
 namespace fs=std::filesystem;
 // Never remove a shared fixed-name temp directory belonging to another
 // process or a previous test. Create a genuinely new test-owned sandbox.
 const auto base=fs::temp_directory_path();
 const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
 std::error_code ec;
 fs::path root;
 for(unsigned attempt=0;attempt<128;++attempt) {
    const auto candidate=base/("djmeta-cue-transaction-"+
         std::to_string(nonce)+"-"+std::to_string(attempt));
    ec.clear();
    if(fs::create_directory(candidate,ec)) {root=candidate;break;}
    if(ec)break;
 }
 check(!root.empty(),"isolated newly owned fixture root");
 const auto file=root/"synthetic.cue";
 const std::string before="FILE \"a.flac\" WAVE\r\nTITLE \"Original\"\r\n";
 const std::string after="FILE \"a.flac\" WAVE\r\nTITLE \"Updated\"\r\n";
 check(write_bytes(file,before),"fixture initialized");
 CueApplyPreflight approved;
 approved.readiness=CueApplyReadiness::DetachedCandidate;
 approved.postimage.changed=true;
 approved.postimage.bytes=after;
 auto result=commit_fixture_cue(root,file,approved,before,true);
 std::string observed;
 check(result.status==CueFixtureWriteStatus::Restored &&
       read_bytes(file,observed) && observed==before,
       "injected post-backup failure restores exact original bytes");
 result=commit_fixture_cue(root,file,approved,before);
 check(result.status==CueFixtureWriteStatus::Committed &&
       read_bytes(file,observed) && observed==after &&
       !fs::exists(root/".djmeta-cue-fixture-backup"),
       "verified fixture byte change commits and clears backup");
 check(commit_fixture_cue(root,file,approved,before).status==
       CueFixtureWriteStatus::Refused,
       "stale fixture source refuses a repeated write");
 CueApplyPreflight noop;
 noop.readiness=CueApplyReadiness::ExactNoop;
 noop.postimage.bytes=after;
 check(commit_fixture_cue(root,file,noop,after).status==
       CueFixtureWriteStatus::Noop,
       "no-op performs zero file operations");
 check(commit_fixture_cue(root,file,noop,before).status==
       CueFixtureWriteStatus::Refused,
       "stale source does not inherit a no-op result");
 auto dishonest=noop;
 dishonest.postimage.bytes=before;
 check(commit_fixture_cue(root,file,dishonest,before).status==
       CueFixtureWriteStatus::Refused,
       "false no-op cannot pretend the source still has old bytes");
 check(commit_fixture_cue(root,file,CueApplyPreflight{},after).status==
       CueFixtureWriteStatus::Refused,
       "unqualified preflight is always refused");
 check(write_bytes(root/".djmeta-cue-fixture-backup","collision"),
       "fixture backup collision made");
 check(commit_fixture_cue(root,file,approved,after).status==
       CueFixtureWriteStatus::Refused,
       "existing backup is not overwritten");
 fs::remove_all(root,ec);
 check(!ec,"fixture cleanup");
 std::cout<<"PASS: exact-byte fixture CUE transaction, no-op, rollback and collision\n";
}
