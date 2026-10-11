#include "stdafx.h"

#include "guid.h"
#include "preview.h"
#include "prepare_dialog.h"
#include "menu_settings.h"
#include "routing_preview.h"

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
                "Analyzes selected tracks with the shared normalizer ruleset "
                "and displays original and proposed values. Does not write tags.";
        } else if (index == 4) {
            output =
                "Opens the native Prepare Tracks dialog with an editable destination "
                "and Title Formatting expression. Preview only; no file or tag writes.";
        } else {
            output =
                "Evaluates the selected legacy foobar File Operations profile against "
                "SAFE-only staged metadata. No tag writes, renames, moves or copies.";
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
                if (index == 0) show_normalization_preview(retained);
                else if (index == 4) show_prepare_tracks_dialog(retained);
                else show_legacy_route_preview(retained, index - 1);
            });

        metadb_io_v2::get()->load_info_async(
            retained,
            metadb_io::load_info_check_if_changed,
            core_api::get_main_window(),
            metadb_io_v2::op_flag_delay_ui,
            notify);
    }
};

contextmenu_item_factory_t<normalizer_context_menu> g_context_item_factory;

} // namespace
} // namespace djmeta_foobar
