#pragma once
// The foobar-only bridge from an explicitly reviewed external-CUE edition
// to the native Windows transactional byte sink. Never called by normal
// read-only builds; never treats a virtual subsong as a physical tag writer.
#include <SDK/foobar2000.h>
#include "djmeta/batch_plan.h"
#include "djmeta/cue_review_postimage.h"
#include "../writer/external_cue_transaction_win.h"
#include <cstddef>
#include <string>
#include <vector>

namespace djmeta_foobar {
struct ExternalCueHostReview {
    std::string source_path;
    std::wstring native_path;
    std::string original_bytes;
    std::string physical_key;
    std::vector<djmeta::FilePlanItem> items;
    djmeta::BatchApproval approval;
    std::size_t cue_index = 0;
    std::vector<std::size_t> selected_indices;
    djmeta::CueValuePostimage postimage;
};

// Pure qualification and detached batch approval fingerprint; NO write.
ExternalCueHostReview prepare_external_cue_host_review(
    const metadb_handle_ptr& cue_handle, const std::string& cue_path,
    const djmeta::online::ReleaseEdition& edition,
    const std::vector<std::size_t>& selected_indices,
    const djmeta::CueValuePostimage& displayed_postimage);

// Call only AFTER the user has separately confirmed all selected exact
// before/after fields. Re-probes CUE and every FILE participant, recomputes
// official edition mapping and immutable plan, then invokes the native sink.
djmeta::online::ExternalCueCommitResult commit_confirmed_external_cue_review(
    const ExternalCueHostReview& reviewed, const metadb_handle_ptr& cue_handle,
    const djmeta::online::ReleaseEdition& edition);
} // namespace djmeta_foobar
