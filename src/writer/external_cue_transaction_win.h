#pragma once
// Native external-CUE byte transaction. This is an isolated OS sink, NOT an
// approval or provider-evidence authority. It must only receive a fresh
// CueApplyPreflight from the host batch gate after explicit user consent.
// This source is deliberately NOT linked into the foobar component yet.
#include "djmeta/cue_apply_preflight.h"
#include <string>
#include <string_view>

namespace djmeta::online {
enum class ExternalCueCommitStatus {
    Refused, ExactNoop, Committed, BackupRetained
};
struct ExternalCueCommitResult {
    ExternalCueCommitStatus status = ExternalCueCommitStatus::Refused;
    std::string reason;
    std::wstring retained_backup;
};
ExternalCueCommitResult commit_preflighted_external_cue_windows(
    const std::wstring& native_path,
    std::string_view expected_physical_key,
    std::string_view reviewed_source_bytes,
    const CueApplyPreflight& preflight);
} // namespace djmeta::online
