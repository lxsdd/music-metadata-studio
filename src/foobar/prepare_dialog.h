#pragma once

#include <SDK/foobar2000.h>

namespace djmeta_foobar {

// Native, modeless single-instance Studio workspace. This entry point
// selects only an initial route preview; existing decisions are preserved
// and no metadata, CUE or filesystem write is performed.
void show_prepare_tracks_dialog(const metadb_handle_list& handles);

} // namespace djmeta_foobar
