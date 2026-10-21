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
    std::cout << "PASS: Windows OS file identity and read-only guard semantics\n";
}
