# OpenRAR Architecture

OpenRAR is organized as a layered, zero-dependency C++20 codebase. The core engine, CLI, SFX stub, and test suite compile identically on Windows (MSVC) and Linux/macOS (GCC/Clang), while Windows-specific UI code stays isolated under `src/gui/`.

---

## Layer Diagram

```text
+-----------------------------------------------------------------------+
|                       Presentation / Entrypoints                      |
|  +---------------------+  +------------------+  +------------------+  |
|  | OpenRAR-GUI (Win32) |  |  openrar (CLI)   |  | openrar_sfx_stub |  |
|  |   src/gui/*.cpp     |  |  src/cli/main.cpp|  | src/sfx/sfx_stub |  |
|  +----------+----------+  +--------+---------+  +--------+---------+  |
+-------------|----------------------|---------------------|------------+
              |                      |                     |
              v                      v                     v
+-----------------------------------------------------------------------+
|                 Unified Facade (`ArchiveManager`)                     |
|   include/openrar/archive_manager.hpp + src/core/archive_manager.cpp  |
|   list, create, extract, test, add, delete, comment, repair,          |
|   convert, find_files, create_sfx, run_benchmark, shell integration   |
+-----------------------------------------------------------------------+
              |
              +------------------+------------------+------------------+
              v                  v                  v                  v
+-------------------+  +------------------+  +--------------+  +-----------------+
| OrarArchive       |  | ZipArchive       |  | TarArchive   |  | ExtendedArchive |
| (.orar)           |  | (.zip / ZIP64)   |  | (.tar/.gz)   |  | (RAR/7z/XZ/BZ2) |
+-------------------+  +------------------+  +--------------+  +-----------------+
              \                  |                  /                  |
               v                 v                 v                   v
+-----------------------------------------------------------------------+
|                        Core Primitives Layer                          |
|  - codec/deflate: RFC 1951 Deflate (Dynamic+Fixed+Lazy) + 9-bit O(1)  |
|                   Inflate + RFC 1952 GZip                             |
|  - crypto:        CRC32, SHA-256, SHA-1, HMAC, PBKDF2, AES-256-GCM,   |
|                   AES-256-CTR (AE-2), ZipCrypto, OS CSPRNG            |
|  - recovery:      Block-interleaved Parity Recovery Record & Repair   |
|  - binary_io:     Little-endian readers/writers & split-volume loader |
|  - i18n:          13-language UTF-8 localization & OS locale detect   |
|  - types:         Shared value types, path safety, wildcard matching  |
+-----------------------------------------------------------------------+
```

---

## Module Map

### `include/openrar/` and `src/core/` (Static Library `openrar_core`)
| Module | Responsibility |
| :--- | :--- |
| `types.hpp/.cpp` | Domain models (`EntryInfo`, `ArchiveInfo`, `CreateOptions`, `ExtractOptions`, `OperationResult`), Zip-Slip guard (`is_safe_relative_path`), UTF-8 path helpers (`path_to_utf8`/`path_from_utf8`), wildcard matching, portable timestamps, binary file I/O |
| `i18n.hpp/.cpp` | Built-in UTF-8 localization tables for 13 languages (`en`, `zh`, `es`, `hi`, `ar`, `pt`, `id`, `fr`, `ja`, `ru`, `sr`, `hr`, `bs`) and OS locale auto-detection |
| `binary_io.hpp` | Header-only little-endian readers/writers (`read_u16/32/64`, `write_u16/32/64`) and `.partN.<ext>` split-volume reassembly |
| `crypto.hpp/.cpp` | CRC32, SHA-256, SHA-1, HMAC-SHA256/SHA1, PBKDF2-HMAC-SHA256/SHA1, AES-256, AES-256-GCM, AES-256-CTR, ZipCrypto, OS CSPRNG, constant-time comparison |
| `deflate.hpp/.cpp` | RFC 1951 Deflate (Canonical Dynamic Huffman, Fixed Huffman, Stored fallback, lazy LZ77 matching), 9-bit O(1) Inflate decoder, RFC 1952 GZip |
| `recovery.hpp/.cpp` | Interleaved 512-byte sector parity generator and burst-error repair engine |
| `orar_archive.hpp/.cpp` | Native `.orar` container: solid & parallel compression, AES-256-GCM payload & header encryption, recovery records, split volumes, comments |
| `zip_archive.hpp/.cpp` | PKWARE ZIP & ZIP64 (>4 GB / >65,535 entries), parallel compression, standard AE-2 AES-256 & ZipCrypto, comments, add/delete, header-scan repair, split volumes |
| `tar_archive.hpp/.cpp` | POSIX USTAR + GNU LongName `.tar` and `.tar.gz` |
| `rar_inspector.hpp/.cpp` | Zero-dependency RAR4 and RAR5 binary header/entry parser |
| `extended_archive.hpp/.cpp` | Cross-platform libarchive (`bsdtar`) and `unrar`/`7z` backend for `.rar`, `.7z`, `.tar.xz`, `.tar.bz2`, `.tar.zst`, `.cab`, `.iso` |
| `archive_manager.hpp/.cpp` | Single public facade dispatching to format handlers plus Search, Batch Convert, SFX builder, Multi-threaded Benchmark, and Windows HKCU Shell integration |

### `src/gui/` (Windows Win32 GUI: `OpenRAR-GUI.exe`)
| File | Responsibility |
| :--- | :--- |
| `gui_common.hpp` | Control/Menu IDs, UTF-8/UTF-16 & `tr_w`/`tr_w_plain` localization helpers, date/number formatting, Shell file/folder dialogs |
| `gui_config.hpp` | Persistent `%APPDATA%\OpenRAR\config.ini` (Favorites, last directory, window size, and language code) |
| `toolbar_icons.hpp` | GDI+ vector renderer for the 9 toolbar actions |
| `gui_toolbar.hpp/.cpp` | Double-buffered custom 9-button desktop toolbar control |
| `gui_dialogs.hpp/.cpp` | Modal dialogs: Input prompt, Wizard, Add-to-Archive, Extract options, and non-blocking multi-threaded Live Progress dialog |
| `main_win32.cpp` | Main window layout, filesystem + hierarchical archive browser, context menus, accelerators, and `wWinMain` |
| `app.rc` / `app.manifest` | Embedded multi-size `assets/openrar.ico` and ComCtl32 v6 / Per-Monitor DPI manifest |

---

## OpenRAR (`.orar`) Binary Format

```text
+-------------------------------------------------------------------+
| Fixed Header (135 bytes)                                          |
|   Magic: "ORAR\x01\x00" (6B) | Flags (4B) | RecoveryPct (4B)      |
|   Level (1B) | PBKDF2 Salt (16B) | Hdr Nonce/Tag (28B)            |
|   Data Nonce/Tag (28B) | Section Offsets & Sizes (6 x 8B)         |
+-------------------------------------------------------------------+
| File Table (Comment + Entry Metadata + CRC32 + SHA-256)           |
|   [Optionally AES-256-GCM encrypted when FLAG_ENCRYPTED_HEADERS]  |
+-------------------------------------------------------------------+
| Payload (Per-file compressed OR single Solid stream)              |
|   [Optionally AES-256-GCM encrypted when FLAG_ENCRYPTED]          |
+-------------------------------------------------------------------+
| Optional Recovery Record ("RECR" magic + Sector CRCs + Parity)    |
|   Protects [File Table + Payload] against bit & burst corruption  |
+-------------------------------------------------------------------+
```

---

## Core Design Rules

1. **One-way dependencies**: `gui` / `cli` / `sfx` -> `ArchiveManager` -> format handlers -> primitives. Never introduce reverse edges.
2. **Offline zero-dependency core**: `openrar_core` compiles with only the C++20 standard library and OS system APIs.
3. **Security defaults**: Every extracted path passes `is_safe_relative_path()` (Zip-Slip guard), all MACs/tags compare via `crypto::constant_time_equal()`, and `.orar` entries verify both CRC32 and SHA-256.
4. **Responsive GUI**: Heavy operations always execute on a worker thread via `run_with_progress()`, never blocking the Win32 message loop.

---

## Knowledge Graph and Status Tracking

- Machine-readable module, feature, and hotspot graph: [docs/KNOWLEDGE_GRAPH.json](docs/KNOWLEDGE_GRAPH.json)
- Agent onboarding and Done / Not-Done roadmap: [AGENTS.md](AGENTS.md)
- Contributor workflow and style guide: [CONTRIBUTING.md](CONTRIBUTING.md)
