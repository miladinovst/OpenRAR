# Contributing to OpenRaR

Thank you for contributing to OpenRaR! This guide covers local setup, code organization, and how to safely add features.

## Local Build and Tests

### Windows
```powershell
.\build.ps1 -Test
# Or also launch the desktop GUI:
.\build.ps1 -Test -RunGui
```

### Linux / macOS
```bash
./build.sh
```

All changes must compile with **0 warnings and 0 errors** under MSVC `/W4` and GCC/Clang `-Wall -Wextra -Wpedantic`, and pass all tests in `tests/test_openrar.cpp`.

## Where Things Live

- **Adding a new archive format**:
  1. Add a value to `ArchiveFormat` in `include/openrar/types.hpp` and update detection helpers in `src/core/types.cpp`.
  2. Implement the format handler in `include/openrar/<fmt>_archive.hpp` and `src/core/<fmt>_archive.cpp`, reusing `include/openrar/binary_io.hpp` for little-endian serialization.
  3. Wire dispatch in `src/core/archive_manager.cpp` and register the new `.cpp` in `CMakeLists.txt`.
  4. Add roundtrip tests to `tests/test_openrar.cpp`.
- **Adding a GUI dialog or command**:
  1. Add the control/menu ID in `src/gui/gui_common.hpp`.
  2. Put modal dialogs in `src/gui/gui_dialogs.hpp/.cpp` using `run_modal_dialog()` and wrap slow work in `run_with_progress()`.
  3. Wire menu/hotkey/context-menu dispatch in `src/gui/main_win32.cpp`.

## Code Style

- C++20, 4-space indentation, UTF-8, LF line endings (see `.editorconfig` and `.clang-format`).
- Document every public struct, enum, and function in `include/openrar/*.hpp` with concise `///` comments.
- Keep comments focused on *why* (invariants, wire layout, edge cases), not restating obvious syntax.
- Update `docs/KNOWLEDGE_GRAPH.json` and `AGENTS.md` when adding modules or completing roadmap items.
