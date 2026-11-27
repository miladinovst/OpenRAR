# AGENTS.md: AI Agent Onboarding and Project Status

> **Machine-readable Knowledge Graph:** Read [docs/KNOWLEDGE_GRAPH.json](docs/KNOWLEDGE_GRAPH.json) first for the complete module graph, dependency edges, feature status (`done` / `not_done`), maintenance hotspots, and architectural invariants.
> **System Design & Binary Specs:** Read [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Quick Build and Verify

- **Windows (MSVC + Ninja):** `.\build.ps1 -Test` (add `-RunGui` to launch `OpenRAR-GUI.exe`)
- **Linux / macOS (GCC/Clang):** `./build.sh`
- **Zero-warning gate:** MSVC `/W4` and GCC/Clang `-Wall -Wextra -Wpedantic` must stay at **0 warnings, 0 errors**.

---

## Current Status Matrix (Done vs. Not Done)

### Done and Verified
- [x] **Core Crypto (`crypto.hpp/.cpp`)**: CRC32, SHA-256, SHA-1, HMAC-SHA256/SHA1, PBKDF2-HMAC-SHA256/SHA1, AES-256, AES-256-GCM, AES-256-CTR, ZipCrypto, OS CSPRNG (`BCryptGenRandom` / `/dev/urandom`).
- [x] **Compression Codec (`deflate.hpp/.cpp`)**: RFC 1951 Deflate with Canonical Dynamic Huffman (type 2), Fixed Huffman (type 1), Stored auto-fallback, lazy LZ77 matching, 9-bit O(1) fast Inflate table, RFC 1952 GZip.
- [x] **Recovery Records (`recovery.hpp/.cpp`)**: 512-byte sector CRCs + interleaved XOR parity repairing bit and burst corruptions bit-for-bit.
- [x] **OpenRAR Native Format (`orar_archive.hpp/.cpp`)**: Solid & parallel multi-threaded compression, AES-256-GCM, `-hp` encrypted headers, `-rr` recovery records, `.partN.orar` split volumes, comments, CRC32 + SHA-256.
- [x] **ZIP / ZIP64 (`zip_archive.hpp/.cpp`)**: ZIP64 (>4 GB / >65,535 files), parallel compression, standard **AE-2 AES-256** & ZipCrypto, UTF-8, add/delete, comments, `.partN.zip` split volumes, header-scan repair.
- [x] **TAR / TAR.GZ (`tar_archive.hpp/.cpp`)**: POSIX USTAR + GNU LongName.
- [x] **RAR4/RAR5 + 7z + Extended (`rar_inspector.cpp`, `extended_archive.cpp`)**: Native RAR4/RAR5 header inspector + silent libarchive (`bsdtar`) & `unrar`/`7z` backend for `.rar`, `.7z` (create + extract), `.tar.xz`, `.tar.bz2`, `.tar.zst`, `.cab`, `.iso`.
- [x] **Unified Services (`archive_manager.cpp`)**: Batch Convert, Find-in-Archives (wildcard + text), real SFX builder (`openrar_sfx_stub`), multi-threaded Benchmark (`2,641 MB/s` on 24 threads), HKCU Windows Explorer right-click integration.
- [x] **CLI (`src/cli/main.cpp`)**: Cross-platform CLI (`a`, `x`, `e`, `l`, `t`, `d`, `c`, `r`, `f`, `cv`, `sfx`, `b`, `info`).
- [x] **Modular Windows Win32 GUI (`src/gui/`)**: Clean flat vector icon (`assets/openrar.svg` / `assets/openrar.ico` embedded via `app.rc`), 9-button GDI+ toolbar, hierarchical folder browsing inside archives, right-click context menus, Drag & Drop in, non-blocking Live Progress dialog (MB/s + Cancel), Wizard, persistent `%APPDATA%\OpenRAR\config.ini`.
- [x] **Internationalization (`i18n.hpp/.cpp`)**: Built-in UTF-8 localization for 13 languages (`en`, `zh`, `es`, `hi`, `ar`, `pt`, `id`, `fr`, `ja`, `ru`, `sr`, `hr`, `bs`), automatic OS locale detection, live Win32 GUI `Options -> Language` switcher, and CLI `--lang=<code>` / `OPENRAR_LANG` / `openrar lang`.
- [x] **Cross-Platform Installers & Release Pipeline (`installer/`, `scripts/package_release.ps1`, `.github/workflows/release.yml`)**: Windows per-user `install.ps1`/`uninstall.ps1` + Inno Setup 6 `openrar.iss` (`OpenRAR-Setup-*.exe`), Linux/macOS `install.sh`/`uninstall.sh` + CPack `.deb`/`.tar.gz`, and tag-triggered GitHub Release automation with `SHA256SUMS.txt`.
- [x] **Cross-Platform CI (`.github/workflows/ci.yml`)**: Automated builds & `ctest` on `windows-latest` (MSVC) and `ubuntu-latest` (GCC & Clang).
- [x] **Automated Tests (`tests/test_openrar.cpp`)**: 11 end-to-end and stress suites passing (Crypto NIST/RFC6070, Deflate multi-block/fuzz, ZIP AE-2/ZipCrypto/Unicode/Repair/Split, OpenRAR Solid/Parallel/Burst Repair/Split/Unicode, Extract Modes + Zip-Slip guard, TAR GNU LongName, 7z/Convert/Find/Benchmark, RAR5 Inspector, Real SFX Execution, Real CLI Process, 13-Language i18n Completeness).

### Not Done / Roadmap (Next High-Value Tasks)
- [ ] **Chunked Streaming I/O for Multi-GB Archives**: `ZipArchive` (`zip_archive.cpp:build_zip_bytes`) and `OrarArchive` (`orar_archive.cpp:build_and_write_orar`) currently buffer full archives in RAM; refactor to stream via `std::ifstream`/`std::ofstream` for 10 GB+ files.
- [ ] **Drag-Out Extraction (Windows GUI)**: Implement OLE `IDropSource` / `CF_HDROP` in `src/gui/main_win32.cpp` to drag files out of an open archive directly into Explorer.
- [ ] **Optional Folder Tree & Comment Side Panel (Windows GUI)**: Collapsible left folder tree and right archive comment pane.
- [ ] **Native High-Ratio Codec for `.orar`**: Optional Zstandard or LZMA2 codec inside `.orar` for tighter ratios on large binaries.
- [ ] **Cross-Platform Linux Desktop GUI**: Optional GTK4 / Qt6 / ImGui frontend calling `openrar_core`.

---

## Rules for Contributors and AI Agents

1. **Strict Layering**: `gui` / `cli` / `sfx` -> `ArchiveManager` -> format handlers -> primitives (`deflate`, `crypto`, `recovery`, `binary_io`, `types`). Never introduce reverse dependencies.
2. **Zero External Dependencies in `openrar_core`**: Keep `openrar_core` self-contained C++20 so it builds offline on Windows and Linux.
3. **No `<windows.h>` in `include/openrar/`**: Keep all public headers 100% OS-agnostic.
4. **Security Invariants**: Always validate extracted paths via `is_safe_relative_path()` (Zip-Slip protection) and compare MACs/tags with `crypto::constant_time_equal()`.
5. **Trademark Cleanliness**: Never use third-party product names or logos as OpenRAR branding; reference external formats (`.rar`, `.zip`, `.7z`, etc.) strictly as format identifiers.
6. **Keep Docs & Knowledge Graph Updated**: Whenever you add a module, complete a roadmap item, or change invariants, update [docs/KNOWLEDGE_GRAPH.json](docs/KNOWLEDGE_GRAPH.json) and this file.
