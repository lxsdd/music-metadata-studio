#include "stdafx.h"

#include "batch_preview_dialog.h"
#include "batch_table_settings.h"
#include "legacy_routing_profiles.h"
#include "prepare_dialog.h"
#include "routing_preview.h"

namespace djmeta_foobar {

void show_prepare_tracks_dialog(const metadb_handle_list& handles) {
    // One window instead of the old two-stage wizard. Users can switch
    // routing profiles and edit one or multiple tracks directly in the
    // integrated read-only batch preview.
    const auto defaults = load_workspace_defaults();
    const LegacyMoveRoute& initial = legacy_move_routes[defaults.routing_profile];
    const RoutePreviewChoice choice{
        initial.name, initial.destination_root, initial.foobar_titleformat};
    show_batch_preview_dialog(handles, choice);
}

} // namespace djmeta_foobar
