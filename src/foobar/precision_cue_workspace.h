#pragma once
#include "stdafx.h"
namespace djmeta_foobar {
#ifdef MMS_DIALOG_DISPATCH_QA
void run_precision_original_dialog_qa(HWND studio, void (*invalidate)(HWND));
#endif
inline constexpr UINT kPrecisionCueBackToStudio = WM_APP + 0x6B0;
HWND studio_precision_cue_view(HWND studio);
void open_studio_precision_cue_generator(HWND studio, metadb_handle_list_cref handles);
void close_studio_precision_cue_generator(HWND studio);
}
