#include "stdafx.h"

#include "guid.h"
#include "batch_preview_dialog.h"
#include "legacy_routing_profiles.h"
#include "prepare_dialog.h"
#include "menu_settings.h"
#include "routing_preview.h"
#include "djmeta/single_source_tag_load.h"
#include "host_file_probe.h"

namespace djmeta_foobar {
namespace {

contextmenu_group_popup_factory g_context_group(
    guids::context_group,
    contextmenu_groups::root,
    "Music Metadata Studio",
    0);

class normalizer_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return guids::context_group; }
    unsigned get_num_items() override { return 5; }

    // Inherit the user's existing Preferences > Display > Context Menu
    // visibility choices. New optional routes start out hidden but can be
    // enabled in foobar; this is a constant SDK default, not a runtime veto.
    t_enabled_state get_enabled_state(unsigned index) override {
        return index == 4 ? contextmenu_item::DEFAULT_ON
                          : contextmenu_item::DEFAULT_OFF;
    }

    void get_item_name(unsigned index, pfc::string_base& output) override {
        if (index >= menu_caption_count) return;
        const std::string caption = effective_menu_caption(index);
        output = caption.c_str();
    }

    GUID get_item_guid(unsigned index) override {
        switch (index) {
        case 0: return guids::preview_normalization;
        case 1: return guids::preview_route_singles;
        case 2: return guids::preview_route_alben;
        case 3: return guids::preview_route_livesets;
        case 4: return guids::prepare_tracks;
        default: return pfc::guid_null;
        }
    }

    bool get_item_description(unsigned index, pfc::string_base& output) override {
        if (index > 4) return false;
        if (index == 0) {
            output =
                "Opens the single-instance modeless Studio workspace for local "
                "normalization review. Only explicit decisions can affect routing; "
                "nothing is written.";
        } else if (index == 4) {
            output =
                "Opens the modeless Studio workspace with editable routing and "
                "metadata review. Preview only; no file or tag writes.";
        } else {
            output =
                "Opens the same Studio workspace with this legacy route preset. "
                "An existing batch and its decisions are never replaced. "
                "No tags, files or CUEs are written.";
        }
        return true;
    }

    bool context_get_display(
        unsigned index,
        metadb_handle_list_cref data,
        pfc::string_base& output,
        unsigned& display_flags,
        const GUID& caller) override {
        if (index > 4 || data.get_count() == 0) return false;
        return contextmenu_item_simple::context_get_display(
            index, data, output, display_flags, caller);
    }

    void context_command(
        unsigned index,
        metadb_handle_list_cref data,
        const GUID&) override {
        if (index > 4 || data.get_count() == 0) return;

        metadb_handle_list retained = data;
        completion_notify::ptr notify = fb2k::makeCompletionNotify(
            [retained, index](unsigned status) {
                if (status != metadb_io::load_info_success) {
                    popup_message::g_show(
                        "Unable to load all metadata. "
                        "Nothing was changed.",
                        "Music Metadata Studio");
                    return;
                }
                // All five entry points share the same modeless workspace.
                // The requested route is an initial preview preset only;
                // show_batch_preview_dialog preserves any existing batch.
                if (index == 0 || index == 4) {
                    show_prepare_tracks_dialog(retained);
                } else {
                    const std::size_t route_index =
                        static_cast<std::size_t>(index - 1);
                    if (route_index >= legacy_move_route_count) return;
                    const LegacyMoveRoute& preset =
                        legacy_move_routes[route_index];
                    show_batch_preview_dialog(retained, RoutePreviewChoice{
                        preset.name, preset.destination_root,
                        preset.foobar_titleformat}, true);
                }
            });

        // A single explicit physical source must be read from the actual
        // file, not an unverified recently imported foobar cache snapshot.
        // A host context command may arrive immediately after /add: the
        // cached TITLE can still be empty although the FLAC is tagged.
        // Larger batches retain change-only reads for library performance.
        // Virtual subsongs must never be mistaken for physical sources.
        const bool single_physical = retained.get_count() == 1 &&
            retained[0]->get_subsong_index() == 0;
        // Never force the network/virtual streaming handler: a real native
        // file identity must already be observable with read-only handles.
        const bool native_existing = single_physical &&
            probe_host_file_readonly(retained[0]->get_path()).state ==
                HostFileState::ExistingFile;
        const auto mode = djmeta::single_source_tag_load_policy(
            static_cast<std::size_t>(retained.get_count()),
            single_physical, native_existing);
        metadb_io_v2::get()->load_info_async(
            retained,
            mode == djmeta::SingleSourceTagLoad::ForcePhysicalRefresh
                ? metadb_io::load_info_force
                : metadb_io::load_info_check_if_changed,
            core_api::get_main_window(),
            metadb_io_v2::op_flag_delay_ui,
            notify);
    }
};

contextmenu_item_factory_t<normalizer_context_menu> g_context_item_factory;

} // namespace
} // namespace djmeta_foobar
