#pragma once

#include "gui_common.hpp"

namespace openrar::gui {

// Creates the Classic-style 9-button big-icon toolbar (Add, Extract To, Test, View, Delete, Find, Wizard, Info, | Repair).
// Clicking a button posts WM_COMMAND(cmd_id) to `parent`.
HWND create_app_toolbar(HWND parent, HINSTANCE hinst, int control_id);

} // namespace openrar::gui
