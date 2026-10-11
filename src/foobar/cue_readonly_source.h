#pragma once

#include "djmeta/cue_metadata.h"

#include <SDK/foobar2000.h>

#include <string>

namespace djmeta_foobar {

struct CueReadOnlySource {
    std::string raw_text;   // Exact bytes copied from read-only source.
    djmeta::CueCarrierKind carrier = djmeta::CueCarrierKind::ExternalText;
    std::string diagnostic; // Source type only; never personal metadata.
};

// Intentional read-only, explicit user click:
// - External .cue: resolve via foobar filesystem and open with GENERIC_READ;
//   protect against races with physical source_guard before and after.
// - Embedded textual cue: require physical subsong 0, exactly one existing
//   CUESHEET field from that physical handle. Never use virtual file_info as
//   a writer or infer a new CUE carrier.
// No copying, renaming, target touching, metadata updates or network.
CueReadOnlySource read_cue_raw_on_demand(
    const metadb_handle_ptr& handle, const std::string& source_path);

} // namespace djmeta_foobar
