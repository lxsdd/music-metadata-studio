#include "stdafx.h"

#include "batch_table_settings.h"

#include <SDK/cfg_var.h>
#include <charconv>

namespace djmeta_foobar {
namespace {

constexpr GUID kBatchPreviewWindowSizeGuid =
    {0x03bf8a72, 0xe38f, 0x4a86, {0x8f, 0xab, 0xda, 0x56, 0x13, 0xbc, 0x87, 0x39}};
cfg_string g_batch_preview_window_size(kBatchPreviewWindowSizeGuid, "");
constexpr GUID kBatchPreviewPlacementGuid =
    {0x64c0a912, 0xe4c1, 0x4b53, {0x9d, 0xb1, 0x29, 0xd5, 0x43, 0x3f, 0x57, 0x86}};
cfg_string g_batch_preview_placement(kBatchPreviewPlacementGuid, "");
constexpr GUID kReviewSplitOffsetGuid =
    {0x9c7a2e15, 0x4b60, 0x4cf5, {0x8b, 0xc2, 0x91, 0x30, 0x5f, 0xde, 0x68, 0x41}};
cfg_string g_review_split_offset(kReviewSplitOffsetGuid, "v1|0");


constexpr GUID kOnlineDateTargetGuid =
    {0x60b91d2e,0x1564,0x41b0,{0xa5,0x63,0x72,0x99,0x06,0xd3,0xca,0x27}};
cfg_string g_online_date_target_preference(kOnlineDateTargetGuid,"0");

constexpr GUID kWorkspaceDefaultsGuid =
    {0x251a5bf0,0x691e,0x4ab9,{0x85,0x93,0x37,0xcb,0x50,0xb2,0x5a,0x08}};
cfg_string g_workspace_defaults(kWorkspaceDefaultsGuid, "v1|0|3|0|0");

constexpr GUID kOnlineProviderPriorityGuid =
    {0xc589a0c4, 0x9e6b, 0x4ca7, {0xb2, 0x14, 0x7a, 0x66, 0xa3, 0xfa, 0xa4, 0x18}};
cfg_string g_online_provider_priority(kOnlineProviderPriorityGuid,
    "v1|beatport,crates,discogs,deezer,musicbrainz");

constexpr GUID kBatchTableLayoutGuid =
    {0xbca40f8b, 0x927b, 0x4fe6, {0xa1, 0x75, 0x16, 0x09, 0x63, 0xd1, 0x47, 0x21}};

cfg_string g_batch_table_layout(
    kBatchTableLayoutGuid, "v1|0,1,2,3|220,130,360,180|15|0|0");

// Stable, independent foobar-native settings for review master and detail.
// Never reuse the existing file-grid GUID or change its stored schema.
constexpr GUID kTrackGridGuid =
    {0x912ddaa2, 0x8203, 0x4c92, {0x96, 0xc0, 0xd7, 0xac, 0x83, 0x92, 0x14, 0x11}};
constexpr GUID kDetailGridGuid =
    {0x493e92a1, 0x612e, 0x4bdf, {0xae, 0x50, 0x7e, 0x28, 0x18, 0x85, 0x4f, 0x42}};

cfg_string g_track_grid_layout(kTrackGridGuid,
    "v1|0,1,2,3|265,68,75,85|15|0|0");
cfg_string g_detail_grid_layout(kDetailGridGuid,
    "v1|0,1,2,3,4|150,185,185,88,105|31|0|0");
constexpr GUID kCandidateTrackGridGuid =
    {0x4b921c71,0x591c,0x488e,{0x84,0xf3,0xf5,0x21,0x26,0x11,0x5a,0x72}};
constexpr GUID kCandidateDetailGridGuid =
    {0x32631439,0xaad5,0x4d27,{0xa6,0x29,0x78,0xea,0xb9,0xc2,0xf0,0x13}};
cfg_string g_candidate_track_grid(kCandidateTrackGridGuid,
    "v1|0,1,2,3|280,75,90,85|15|0|0");
cfg_string g_candidate_detail_grid(kCandidateDetailGridGuid,
    "v1|0,1,2,3,4|260,165,130,95,160|31|0|0");
constexpr GUID kOnlineDetailGridGuid =
    {0x859caf22,0xb607,0x4ebe,{0xab,0x62,0xa1,0x2d,0x54,0x6f,0xd6,0x70}};
cfg_string g_online_detail_grid(kOnlineDetailGridGuid,"");

} // namespace

std::optional<djmeta::PreviewWindowPlacement> load_batch_preview_window_placement() {
    const auto raw=g_batch_preview_placement.get();
    return djmeta::parse_preview_window_placement(raw.c_str());
}
void store_batch_preview_window_placement(djmeta::PreviewWindowPlacement value) {
    const auto serialized=djmeta::serialize_preview_window_placement(value);
    if (serialized.empty())
        throw std::invalid_argument("Invalid preview window placement.");
    const auto before=g_batch_preview_placement.get();
    if (std::string_view(before.c_str())!=serialized)
        g_batch_preview_placement.set(serialized.c_str());
}

std::optional<djmeta::PreviewWindowSize> load_batch_preview_window_size() {
    const auto stored = g_batch_preview_window_size.get();
    return djmeta::parse_preview_window_size(stored.c_str());
}

void store_batch_preview_window_size(djmeta::PreviewWindowSize size) {
    const auto serialized = djmeta::serialize_preview_window_size(size);
    if (serialized.empty())
        throw std::invalid_argument("Invalid batch preview window size.");
    const auto before = g_batch_preview_window_size.get();
    if (std::string_view(before.c_str()) != serialized)
        g_batch_preview_window_size.set(serialized.c_str());
}

int load_review_split_offset() {
    const auto raw = g_review_split_offset.get();
    const std::string_view value(raw.c_str());
    if (value.substr(0, 3) != "v1|") return 0;
    int parsed = 0;
    const auto digits = value.substr(3);
    const auto result = std::from_chars(digits.data(),
                                        digits.data() + digits.size(), parsed);
    return result.ec == std::errc{} &&
           result.ptr == digits.data() + digits.size() &&
           parsed >= -2000 && parsed <= 2000 ? parsed : 0;
}
void store_review_split_offset(int logical_offset) {
    if (logical_offset < -2000 || logical_offset > 2000)
        throw std::invalid_argument("Invalid review split offset.");
    const auto encoded = "v1|" + std::to_string(logical_offset);
    const auto before = g_review_split_offset.get();
    if (std::string_view(before.c_str()) != encoded)
        g_review_split_offset.set(encoded.c_str());
}

djmeta::WorkspaceDefaults load_workspace_defaults() {
    const auto saved = g_workspace_defaults.get();
    return djmeta::parse_workspace_defaults(saved.c_str());
}

void store_workspace_defaults(const djmeta::WorkspaceDefaults& settings) {
    const auto encoded = djmeta::serialize_workspace_defaults(settings);
    if (encoded.empty())
        throw std::invalid_argument("Invalid workspace defaults.");
    const auto before = g_workspace_defaults.get();
    if (std::string_view(before.c_str()) != encoded)
        g_workspace_defaults.set(encoded.c_str());
}

djmeta::online::ProviderPreferences load_online_provider_preferences() {
    const auto saved = g_online_provider_priority.get();
    return djmeta::online::parse_provider_preferences(saved.c_str());
}

void store_online_provider_preferences(
    const djmeta::online::ProviderPreferences& settings) {
    const auto encoded = djmeta::online::serialize_provider_preferences(settings);
    if (encoded.empty())
        throw std::invalid_argument("Invalid online provider priority.");
    const auto before = g_online_provider_priority.get();
    if (std::string_view(before.c_str()) != encoded)
        g_online_provider_priority.set(encoded.c_str());
}

int load_online_date_target_preference() {
    const auto raw=g_online_date_target_preference.get();
    const std::string_view stored(raw.c_str());
    // Fail closed on corrupted/legacy settings; never preselect a write.
    if(stored.size()!=1 || stored.front()<'0' || stored.front()>'7')
        return 0;
    return static_cast<int>(stored.front()-'0');
}
void store_online_date_target_preference(int choice) {
    if(choice<0 || choice>7)
        throw std::invalid_argument("Invalid online date target choice.");
    const std::string expected(1,static_cast<char>('0'+choice));
    const auto before=g_online_date_target_preference.get();
    if(std::string_view(before.c_str())!=expected)
        g_online_date_target_preference.set(expected.c_str());
}

djmeta::BatchTableLayout load_batch_table_layout() {
    const auto persisted = g_batch_table_layout.get();
    return djmeta::parse_batch_table_layout(persisted.c_str());
}

void store_batch_table_layout(const djmeta::BatchTableLayout& layout) {
    const auto encoded = djmeta::serialize_batch_table_layout(layout);
    if (encoded.empty())
        throw std::invalid_argument("Invalid preview table layout.");
    g_batch_table_layout.set(encoded.c_str());
}

djmeta::ReviewGridLayout<4> default_track_grid_layout() {
    return djmeta::default_review_grid_layout<4>({265, 68, 75, 85});
}
djmeta::ReviewGridLayout<5> default_detail_grid_layout() {
    return djmeta::default_review_grid_layout<5>({150, 185, 185, 88, 105});
}
djmeta::ReviewGridLayout<4> load_track_grid_layout() {
    // The original unmodified factory layout was far too narrow. Only
    // migrate that *exact* seed, never a customized existing profile.
    const std::string_view saved=g_track_grid_layout.get().c_str();
    if(saved=="v1|0,1,2,3|150,48,48,48|15|0|0")
        return default_track_grid_layout();
    return djmeta::parse_review_grid_layout(saved, default_track_grid_layout());
}
djmeta::ReviewGridLayout<5> load_detail_grid_layout() {
    const std::string_view saved=g_detail_grid_layout.get().c_str();
    if(saved=="v1|0,1,2,3,4|80,105,105,60,75|31|0|0")
        return default_detail_grid_layout();
    return djmeta::parse_review_grid_layout(saved, default_detail_grid_layout());
}
djmeta::ReviewGridLayout<4> load_candidate_track_grid_layout() {
    return djmeta::parse_review_grid_layout(
        std::string_view(g_candidate_track_grid.get().c_str()),
        djmeta::default_review_grid_layout<4>({280,75,90,85}));
}
djmeta::ReviewGridLayout<5> load_candidate_detail_grid_layout() {
    return djmeta::parse_review_grid_layout(
        std::string_view(g_candidate_detail_grid.get().c_str()),
        djmeta::default_review_grid_layout<5>({260,165,130,95,160}));
}
void store_candidate_track_grid_layout(const djmeta::ReviewGridLayout<4>& layout) {
    const auto text=djmeta::serialize_review_grid_layout(layout);
    if(text.empty()) throw std::invalid_argument("Invalid browser track layout.");
    if(std::string_view(g_candidate_track_grid.get().c_str())!=text)
        g_candidate_track_grid.set(text.c_str());
}
void store_candidate_detail_grid_layout(const djmeta::ReviewGridLayout<5>& layout) {
    const auto text=djmeta::serialize_review_grid_layout(layout);
    if(text.empty()) throw std::invalid_argument("Invalid browser detail layout.");
    if(std::string_view(g_candidate_detail_grid.get().c_str())!=text)
        g_candidate_detail_grid.set(text.c_str());
}
void store_track_grid_layout(const djmeta::ReviewGridLayout<4>& layout) {
    const auto text = djmeta::serialize_review_grid_layout(layout);
    if (text.empty()) throw std::invalid_argument("Invalid track grid layout.");
    if(std::string_view(g_track_grid_layout.get().c_str())!=text)
        g_track_grid_layout.set(text.c_str());
}
void store_detail_grid_layout(const djmeta::ReviewGridLayout<5>& layout) {
    const auto text = djmeta::serialize_review_grid_layout(layout);
    if (text.empty()) throw std::invalid_argument("Invalid detail grid layout.");
    if(std::string_view(g_detail_grid_layout.get().c_str())!=text)
        g_detail_grid_layout.set(text.c_str());
}

} // namespace djmeta_foobar

namespace djmeta_foobar {
bool has_saved_online_detail_grid_layout() {
    const auto raw=g_online_detail_grid.get();
    return !std::string_view(raw.c_str()).empty();
}
djmeta::ReviewGridLayout<4> load_online_detail_grid_layout() {
    const auto raw=g_online_detail_grid.get();
    return djmeta::parse_review_grid_layout(
        std::string_view(raw.c_str()),
        djmeta::default_review_grid_layout<4>({155,130,175,90}));
}
void store_online_detail_grid_layout(const djmeta::ReviewGridLayout<4>& layout) {
    const auto encoded=djmeta::serialize_review_grid_layout(layout);
    if(encoded.empty())
        throw std::invalid_argument("Invalid online detail grid layout.");
    if(std::string_view(g_online_detail_grid.get().c_str())!=encoded)
        g_online_detail_grid.set(encoded.c_str());
}
} // namespace djmeta_foobar
