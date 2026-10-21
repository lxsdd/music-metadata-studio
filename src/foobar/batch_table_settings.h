#pragma once

#include "djmeta/table_layout.h"
#include "djmeta/review_grid_layout.h"
#include "djmeta/preview_window_geometry.h"
#include <optional>

namespace djmeta_foobar {

// foobar-native profile configuration, not external JSON/registry.
// Profile-local foobar settings. Persist only changed window size (not
// position, music metadata or session state); 96-DPI logical units.
std::optional<djmeta::PreviewWindowPlacement> load_batch_preview_window_placement();
void store_batch_preview_window_placement(djmeta::PreviewWindowPlacement value);
std::optional<djmeta::PreviewWindowSize> load_batch_preview_window_size();
void store_batch_preview_window_size(djmeta::PreviewWindowSize size);

// Remember only the user's preferred date target choice (0..7). No selected
// provider values, per-track decisions or authorization is persisted.
int load_online_date_target_preference();
void store_online_date_target_preference(int choice);

djmeta::BatchTableLayout load_batch_table_layout();
void store_batch_table_layout(const djmeta::BatchTableLayout& layout);

// Separate foobar cfg variables; file-grid layouts remain backward compatible.
djmeta::ReviewGridLayout<4> default_track_grid_layout();
djmeta::ReviewGridLayout<5> default_detail_grid_layout();
djmeta::ReviewGridLayout<4> load_track_grid_layout();
djmeta::ReviewGridLayout<5> load_detail_grid_layout();
void store_track_grid_layout(const djmeta::ReviewGridLayout<4>& layout);
void store_detail_grid_layout(const djmeta::ReviewGridLayout<5>& layout);
// Browser layouts never borrow local rule-review widths or reset on track changes.
djmeta::ReviewGridLayout<4> load_candidate_track_grid_layout();
djmeta::ReviewGridLayout<5> load_candidate_detail_grid_layout();
void store_candidate_track_grid_layout(const djmeta::ReviewGridLayout<4>& layout);
void store_candidate_detail_grid_layout(const djmeta::ReviewGridLayout<5>& layout);

} // namespace djmeta_foobar
