#pragma once
// Dedicated SDK writer backend. No PreviewState/HWND lifetime dependencies.
// Not invoked by the production read-only UI until isolated host QA is complete.
#include "djmeta/physical_write_contract.h"
#include <SDK/foobar2000.h>
#include <functional>
#include <string>
namespace djmeta_foobar {
enum class PhysicalWriteOutcome {
    Refused, Noop, Submitted, Completed, Cancelled, Failed, ReadbackMismatch
};
struct PhysicalWriteReport {
    PhysicalWriteOutcome outcome=PhysicalWriteOutcome::Refused;
    std::string diagnostic;
};
using PhysicalWriteCompletion=std::function<void(PhysicalWriteReport)>;
PhysicalWriteReport submit_frozen_physical_tag_write(
    const metadb_handle_ptr& source,
    const djmeta::online::FrozenPhysicalTagWrite& frozen,
    PhysicalWriteCompletion completion);
}
