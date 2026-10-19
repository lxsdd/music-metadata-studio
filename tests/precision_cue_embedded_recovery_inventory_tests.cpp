#include "djmeta/precision_cue_embedded_recovery_inventory.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"Precision CUE recovery inventory failed %d: %s\n",\
        __LINE__,#x); std::exit(1); } } while(false)
namespace {
namespace fs=std::filesystem;
struct Sandbox {
    fs::path directory;
    Sandbox() {
        std::random_device random;
        directory=fs::canonical(fs::temp_directory_path()) /
            ("precision-cue-recovery-scan-"+std::to_string(random())+
             "-"+std::to_string(random()));
        CHECK(fs::create_directory(directory));
    }
    ~Sandbox() {
        std::error_code ignored;
        fs::remove_all(directory,ignored);
    }
};
void save(const fs::path& name,std::string_view contents) {
    std::ofstream file(name,std::ios::binary);
    CHECK(file.good());
    file.write(contents.data(),static_cast<std::streamsize>(contents.size()));
    CHECK(file.good());
}
std::string contents(const fs::path& name) {
    std::ifstream file(name,std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
constexpr std::string_view id1="00000000000000000000000000000001";
constexpr std::string_view id2="00000000000000000000000000000002";
constexpr std::string_view id3="00000000000000000000000000000003";
fs::path candidate(const Sandbox& area,bool published,std::string_view id) {
    return area.directory /
        (std::string(published ?
            ".precision-cue-backup-verified-" :
            ".precision-cue-backup-pending-") +
         std::string(id) + (published ? ".bak" : ".tmp"));
}
void readonly_inventory_and_crash_cases() {
    using Status=PrecisionCueRecoveryInventoryStatus;
    using State=PrecisionCueRecoveryCandidateState;
    Sandbox area;
    const auto source=area.directory/"synthetic.mp3";
    const std::string prefix="ID3" + std::string(2045,'P');
    const auto bytes=prefix + std::string(70005,'s');
    save(source,bytes);
    auto snapshot=capture_precision_cue_physical_full_snapshot(
        source,prefix);
    CHECK(snapshot.has_value());
    const auto empty=inventory_precision_cue_recovery_backups(
        *snapshot,prefix);
    CHECK(empty.status==Status::Complete);
    CHECK(empty.candidates.empty());

    const auto good=candidate(area,true,id1);
    const auto damaged=candidate(area,true,id2);
    const auto incomplete=candidate(area,false,id3);
    save(good,bytes);
    save(damaged,bytes.substr(0,bytes.size()-1)+"X");
    save(incomplete,bytes);
    auto list=inventory_precision_cue_recovery_backups(*snapshot,prefix);
    CHECK(list.status==Status::Complete);
    CHECK(list.candidates.size()==3);
    CHECK(list.inspected==3);
    bool found_good=false,found_bad=false,found_pending=false;
    for(const auto& item:list.candidates) {
        if(item.path==good) {
            CHECK(item.state==State::VerifiedMatchesReviewedSource);
            found_good=true;
        }
        if(item.path==damaged) {
            CHECK(item.state==State::VerifiedMismatchOrUnsafe);
            found_bad=true;
        }
        if(item.path==incomplete) {
            CHECK(item.state==State::PendingUntrusted);
            found_pending=true;
        }
    }
    CHECK(found_good && found_bad && found_pending);
    // Case, suffix and length mismatches are not owned by Precision CUE.
    const auto foreign=area.directory/".precision-cue-backup-verified-wrong.bak";
    save(foreign,bytes);
    const auto uppercase=candidate(area,true,
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    save(uppercase,bytes);
    const auto long_id=candidate(area,true,
        "000000000000000000000000000000000");
    save(long_id,bytes);
    CHECK(inventory_precision_cue_recovery_backups(
        *snapshot,prefix).inspected==3);
    CHECK(contents(foreign)==bytes);
    CHECK(contents(uppercase)==bytes);
    CHECK(contents(long_id)==bytes);

    // Do not list a truncated set of apparently safe recovery candidates.
    auto capped=inventory_precision_cue_recovery_backups(*snapshot,prefix,2);
    CHECK(capped.status==Status::TooManyCandidates);
    CHECK(capped.candidates.empty());
    CHECK(inventory_precision_cue_recovery_backups(
        *snapshot,prefix,0).status==Status::UnqualifiedDirectory);
    CHECK(inventory_precision_cue_recovery_backups(
        *snapshot,prefix,129).status==Status::UnqualifiedDirectory);

    // The source can disappear in a crash. Verified backup bytes remain
    // independently testable, but no automatic restoration may happen.
    const auto missing=area.directory/"preserved-source.keep";
    fs::rename(source,missing);
    list=inventory_precision_cue_recovery_backups(*snapshot,prefix);
    CHECK(list.status==Status::SourceChangedOrMissing);
    CHECK(list.candidates.size()==3);
    CHECK(!fs::exists(source));
    CHECK(contents(good)==bytes);
    CHECK(contents(missing)==bytes);
}
void symlinks_never_count_as_trusted_backups() {
#ifndef _WIN32
    using State=PrecisionCueRecoveryCandidateState;
    Sandbox area;
    const auto source=area.directory/"synthetic.mp3";
    const std::string prefix="ID3" + std::string(400,'Z');
    const auto bytes=prefix+"SYNTHETIC";
    save(source,bytes);
    auto review=capture_precision_cue_physical_full_snapshot(
        source,prefix);
    CHECK(review.has_value());
    const auto alias=candidate(area,true,id1);
    fs::create_symlink(source,alias);
    const auto list=inventory_precision_cue_recovery_backups(*review,prefix);
    CHECK(list.candidates.size()==1);
    CHECK(list.candidates[0].state==State::VerifiedMismatchOrUnsafe);
    CHECK(contents(source)==bytes);
#endif
}
}
int main() {
    readonly_inventory_and_crash_cases();
    symlinks_never_count_as_trusted_backups();
    std::puts("PASS: read-only verified/pending backup inventory, source missing, symlink, bounds");
    return 0;
}
