#pragma once
#include "stdafx.h"
namespace djmeta_foobar {
#ifdef MMS_DIALOG_DISPATCH_QA
void run_precision_original_dialog_qa(HWND studio);
#endif
void open_studio_precision_cue_generator(HWND studio, metadb_handle_list_cref handles);
void close_studio_precision_cue_generator(HWND studio);
}
