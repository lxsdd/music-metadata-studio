#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace djmeta_foobar {

// Non-mutating OS/foobar filesystem observation. This is intentionally
// separate from the portable normalizer and never establishes approval for a
// tag writer, File Operations, CUE rewrite, or final destination naming.
enum class HostFileState { ExistingFile, Missing, Unqualified };

struct HostFileObservation {
    HostFileState state = HostFileState::Unqualified;
    std::string canonical_path;
    std::string host_physical_key; // physical ID, not a normalized path guess
    std::string source_guard;      // physical ID + observed size/times/attributes
    std::string detail;            // human-readable reason if unqualified
};

// Compares two independent snapshots of the SAME literal raw candidate.
// This is still only read-only evidence, never a qualified final FileOps
// destination. Every state must remain stable, including "missing" and
// "unqualified" (which must retain an identical failure reason).
inline bool stable_raw_target_observation(
    const HostFileObservation& before, const HostFileObservation& after) {
    if (before.state != after.state ||
        before.canonical_path.empty() ||
        before.canonical_path != after.canonical_path)
        return false;
    switch (before.state) {
    case HostFileState::ExistingFile:
        return !before.host_physical_key.empty() &&
            !before.source_guard.empty() &&
            before.host_physical_key == after.host_physical_key &&
            before.source_guard == after.source_guard;
    case HostFileState::Missing:
        return before.host_physical_key.empty() &&
            after.host_physical_key.empty() &&
            before.source_guard.empty() && after.source_guard.empty();
    case HostFileState::Unqualified:
        return !before.detail.empty() &&
            before.detail == after.detail &&
            before.host_physical_key.empty() &&
            after.host_physical_key.empty() &&
            before.source_guard.empty() && after.source_guard.empty();
    default:
        return false;
    }
}

// The Windows file index is the identity component, not an optional hint.
// Some virtual/network filesystem implementations return a nonzero volume
// serial but an unusable zero file index (or vice versa). Treat EITHER zero
// as unsupported rather than collapsing distinct music files to one key.
// This pure validation does not read from disk or enable writes.
inline bool valid_native_file_identity(
    std::uint32_t volume_serial, std::uint64_t file_index) noexcept {
    return volume_serial != 0 && file_index != 0 &&
        file_index != UINT64_MAX;
}

// The native handle's flags are checked separately from path attributes.
// A reparse-point file can replace an ordinary source between the initial
// attribute check and CreateFileW; fail closed even with a plausible file ID.
// The two Windows flag values are stable SDK constants (DIRECTORY=0x10,
// REPARSE_POINT=0x400), kept pure for cross-platform regression tests.
inline bool valid_native_regular_file_attributes(std::uint32_t attrs) noexcept {
    return attrs != UINT32_MAX && (attrs & (0x10u | 0x400u)) == 0;
}

// Bounded, read-only native ancestor observation of a LITERAL candidate
// path. It is explicitly not a foobar File Operations final destination
// renderer, does not sanitize metadata and never grants permission to move.
// On missing subdirectories, remember their exact native names and guard all
// EXISTING ancestors with independent OS directory handles. A symlink,
// junction, unsupported physical directory ID, access failure or replacement
// invalidates the entire advisory ancestor chain.
struct HostDestinationAncestor {
    std::wstring native_path;
    bool existing=false;
    std::string physical_key;
    std::string guard;
};
struct HostDestinationParentChain {
    bool qualified=false; // stable raw-parent evidence only, NOT final FileOps
    std::string rejection;
    std::wstring native_target;
    // The user-selected destination ROOT has an independently observed
    // existing directory identity, not just an arbitrary existing parent.
    // Raw target ancestry must contain this very directory; no implicit
    // destination-root creation, drive alias or lexical traversal accepted.
    std::wstring native_destination_root;
    std::string destination_root_physical_key;
    std::string destination_root_guard;
    std::vector<HostDestinationAncestor> ancestors;
};
inline bool stable_raw_destination_parent_chain(
    const HostDestinationParentChain& before,
    const HostDestinationParentChain& after) {
    if(before.qualified!=after.qualified ||
       before.rejection!=after.rejection ||
       before.native_target.empty() ||
       before.native_target!=after.native_target ||
       before.native_destination_root.empty() ||
       before.native_destination_root!=after.native_destination_root ||
       before.destination_root_physical_key!=after.destination_root_physical_key ||
       before.destination_root_guard!=after.destination_root_guard ||
       before.ancestors.size()!=after.ancestors.size() ||
       (before.qualified &&
        (before.destination_root_physical_key.empty() ||
         before.destination_root_guard.empty())))
        return false;
    for(std::size_t i=0;i<before.ancestors.size();++i) {
        const auto& old=before.ancestors[i];
        const auto& now=after.ancestors[i];
        if(old.native_path!=now.native_path ||
           old.existing!=now.existing ||
           old.physical_key!=now.physical_key ||
           old.guard!=now.guard ||
           (old.existing &&
               (old.physical_key.empty() || old.guard.empty())) ||
           (!old.existing &&
               (!old.physical_key.empty() || !old.guard.empty())))
            return false;
    }
    if(before.qualified) {
        bool matched_root=false;
        for(const auto& observed:before.ancestors)
            if(observed.native_path==before.native_destination_root &&
               observed.existing &&
               observed.physical_key==before.destination_root_physical_key &&
               observed.guard==before.destination_root_guard)
                matched_root=true;
        if(!matched_root) return false;
    }
    return true;
}
HostDestinationParentChain probe_raw_destination_parent_chain_readonly(
    const std::string& literal_candidate,
    const std::string& selected_destination_root);

// Missing is a legitimate observation for a proposed target but NOT for a
// selected source. The caller enforces that distinction.
// No files are opened for writing, created, moved or modified.
HostFileObservation probe_host_file_readonly(const std::string& path);

} // namespace djmeta_foobar
