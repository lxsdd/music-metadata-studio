#include "host_file_probe.h"

#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool value, const char* reason) {
    if (!value) {
        std::cerr << "FAIL: " << reason << '\n';
        std::exit(EXIT_FAILURE);
    }
}
int main() {
    using namespace djmeta_foobar;
    require(valid_native_file_identity(199, 10023),
            "normal host file ID is admitted");
    require(!valid_native_file_identity(199, 0),
            "zero file index with nonzero volume serial must be refused");
    require(!valid_native_file_identity(0, 10023),
            "nonzero file index with zero volume serial must be refused");
    require(!valid_native_file_identity(0, 0),
            "missing OS file identity is refused");
    require(!valid_native_file_identity(1, UINT64_MAX),
            "all-bits-set file index is a sentinel, not identity");
    require(valid_native_regular_file_attributes(0x80u),
            "regular FILE_ATTRIBUTE_NORMAL remains inspectable");
    require(valid_native_regular_file_attributes(0x20u | 0x1u),
            "ordinary archive/read-only flags are not rejected");
    require(!valid_native_regular_file_attributes(0x400u | 0x20u),
            "a file handle reporting reparse point must not qualify");
    require(!valid_native_regular_file_attributes(0x10u),
            "a directory handle is never a regular music file");
    require(!valid_native_regular_file_attributes(UINT32_MAX),
            "invalid native attribute sentinel must fail closed");
    const HostFileObservation stable{
        HostFileState::ExistingFile,"file:///C:/Audio/A.mp3",
        "win-file-v1:199:10023","guard-001","Physical source observed"};
    require(stable_raw_target_observation(stable,stable),
            "stable existing target observation is accepted as provisional evidence");
    HostFileObservation different=stable;
    different.host_physical_key="win-file-v1:199:10024";
    require(!stable_raw_target_observation(stable,different),
            "new physical ID invalidates snapshot");
    different=stable;
    different.source_guard="guard-002";
    require(!stable_raw_target_observation(stable,different),
            "source modification invalidates snapshot");
    const HostDestinationParentChain valid_chain{
        true,{},L"C:\\Audio\\Future\\track.flac",
        L"C:\\Audio","win-dir-v1:11:55","guard:stable",{
            {L"C:\\Audio\\Future",false,{},{}},
            {L"C:\\Audio",true,"win-dir-v1:11:55","guard:stable"},
            {L"C:\\",true,"win-dir-v1:11:1","guard:root"}
        }};
    require(stable_raw_destination_parent_chain(valid_chain,valid_chain),
        "missing target child plus independent existing ancestor snapshots stable");
    auto altered_chain=valid_chain;
    altered_chain.ancestors[1].physical_key="win-dir-v1:11:56";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "directory replaced with another physical ID blocks target inspection");
    altered_chain=valid_chain;
    altered_chain.ancestors[1].guard="guard:changed";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "same directory ID but changed attributes blocks target inspection");
    altered_chain=valid_chain;
    altered_chain.ancestors[0].existing=true;
    altered_chain.ancestors[0].physical_key="win-dir-v1:11:99";
    altered_chain.ancestors[0].guard="guard:created";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "previously missing nested directory creation invalidates raw preview");
    altered_chain=valid_chain;
    altered_chain.qualified=false;
    altered_chain.rejection="RAW_PARENT_NOT_DIRECTORY_OR_REPARSE";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "junction or reparse substitution blocks raw ancestor witness");
    altered_chain=valid_chain;
    altered_chain.native_target=L"C:\\Audio\\Other\\track.flac";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "raw candidate substitution changes exact native target identity");
    altered_chain=valid_chain;
    altered_chain.ancestors.pop_back();
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "dropping a root ancestor is not a stable path observation");
    altered_chain=valid_chain;
    altered_chain.ancestors[1].guard.clear();
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "empty observed physical ancestor guard cannot qualify");
    altered_chain=valid_chain;
    altered_chain.native_destination_root=L"C:\\Other";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "switched user-selected destination root cannot reuse old witness");
    altered_chain=valid_chain;
    altered_chain.destination_root_physical_key="win-dir-v1:11:60";
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "destination root physical replacement invalidates raw review");
    altered_chain=valid_chain;
    altered_chain.destination_root_guard.clear();
    require(!stable_raw_destination_parent_chain(valid_chain,altered_chain),
        "missing destination root version guard cannot qualify");
    altered_chain=valid_chain;
    altered_chain.native_destination_root=L"C:\\Audio\\Future";
    altered_chain.destination_root_physical_key="win-dir-v1:11:55";
    altered_chain.destination_root_guard="guard:stable";
    require(!stable_raw_destination_parent_chain(altered_chain,altered_chain),
        "a missing subfolder can never impersonate a selected existing root");
    altered_chain=valid_chain;
    altered_chain.native_destination_root=L"C:\\";
    altered_chain.destination_root_physical_key="win-dir-v1:11:1";
    altered_chain.destination_root_guard="guard:root";
    require(stable_raw_destination_parent_chain(altered_chain,altered_chain),
        "explicitly selected existing drive root remains a valid observed ancestor");
    std::cout << "PASS: Windows OS file identity and read-only guard semantics "
                 "including full target parent chain\n";
}
