# Changelog

All notable changes to OpenRaR are documented in this file.
This project follows [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-11-27

### Added
- **Zero-Dependency C++20 Core (`openrar_core`)**:
  - Self-contained RFC 1951 Deflate (Canonical Dynamic Huffman, Fixed Huffman, Stored auto-fallback, lazy LZ77 with O(1) length/distance lookup tables), 9-bit O(1) Inflate decoder, and RFC 1952 GZip.
  - Self-contained cryptography: CRC32, SHA-256, SHA-1, HMAC-SHA256/SHA1, PBKDF2-HMAC-SHA256/SHA1, AES-256, AES-256-GCM, AES-256-CTR, ZipCrypto, OS CSPRNG, and constant-time MAC comparison.
  - Interleaved 512-byte sector Recovery Records (`-rr`) for bit-for-bit burst-error repair.
  - Native OpenRaR (`.orar`) format with solid & parallel multi-threaded compression, AES-256-GCM, encrypted headers (`-hp`), recovery records, split volumes (`.partN.orar`), comments, and CRC32 + SHA-256 checks.
  - Full PKWARE ZIP / ZIP64 support with standard AE-2 AES-256 & ZipCrypto, UTF-8 filenames, add/delete, comments, split volumes, and header-scan repair.
  - Native TAR / TAR.GZ (POSIX USTAR + GNU LongName).
  - Native RAR4/RAR5 header inspector and silent libarchive (`bsdtar`) / `unrar` / `7z` backend for `.rar`, `.7z`, `.tar.xz`, `.tar.bz2`, `.tar.zst`, `.cab`, and `.iso`.
  - Unified `ArchiveManager` facade with batch format conversion, recursive Find-in-Archives, standalone SFX builder, multi-threaded benchmark, and HKCU Windows Explorer right-click menu registration.
- **13-Language Localization (`i18n`)**:
  - Built-in UTF-8 localization for English, Chinese, Spanish, Hindi, Arabic, Portuguese, Indonesian, French, Japanese, Russian, Serbian, Croatian, and Bosnian with automatic OS locale detection.
- **Desktop GUI, CLI & SFX**:
  - Modular native Win32 GUI (`OpenRaR-GUI.exe`) with flat vector icon, 9-button GDI+ toolbar, hierarchical archive browsing, context menus, Drag & Drop, live progress dialog, Wizard, persistent config, and live `Options -> Language` switching.
  - Cross-platform CLI (`openrar`) and standalone Self-Extracting Archive stub (`openrar_sfx_stub`) for Windows, Linux, and macOS.
- **Installers, CI & Automated Tests**:
  - Windows per-user PowerShell installer (`install.ps1`/`uninstall.ps1`), Inno Setup 6 wizard (`openrar.iss`), Linux/macOS `install.sh`/`uninstall.sh`, CPack rules, local Windows release packager (`scripts/package_release.ps1`), and GitHub Actions CI & Release workflows.
  - 11 automated end-to-end and stress test suites verified at 0 warnings under MSVC `/W4` and GCC/Clang `-Wall -Wextra -Wpedantic`.
