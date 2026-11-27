#pragma once

#include "gui_common.hpp"

#include <atomic>
#include <functional>

namespace openrar::gui {

// Generic single-line, password, or multiline text prompt
bool show_input_dialog(HWND owner, HFONT font,
                       const std::wstring& title, const std::wstring& label,
                       std::wstring& value, bool password = false, bool multiline = false);

// 3-option Wizard: returns 1=Unpack, 2=Create new, 3=Add to existing, 0=Cancelled
int show_wizard_dialog(HWND owner, HFONT font);

// "Archive name and parameters" dialog
bool show_add_dialog(HWND owner, HFONT font, fs::path& out_path, CreateOptions& opts);

// "Extraction path and options" dialog
bool show_extract_dialog(HWND owner, HFONT font, ExtractOptions& opts, bool& open_folder_after);

// Runs a long-running archive operation on a background thread while showing a live
// non-blocking Progress Dialog (progress bar, current file, MB/s speed, elapsed time, Cancel).
OperationResult run_with_progress(
    HWND owner, HFONT font, const std::wstring& title,
    const std::function<OperationResult(ProgressCallback, std::atomic<bool>*)>& task);

} // namespace openrar::gui
