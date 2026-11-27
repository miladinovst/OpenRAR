# Security Policy

## Supported Versions

| Version | Supported |
| :--- | :---: |
| 1.0.x | Yes |

## Built-In Security Invariants

OpenRaR enforces the following security guarantees across `openrar_core`:

1. **Zip-Slip Path Traversal Protection**: Every extracted archive entry path is normalized and validated via `is_safe_relative_path()` before creating directories or writing files. Absolute paths, drive letters, and `..` traversal components are rejected.
2. **Authenticated Encryption**: Native `.orar` archives encrypt payloads and optional headers (`-hp`) with AES-256-GCM and keys derived via PBKDF2-HMAC-SHA256 with OS CSPRNG salts (`BCryptGenRandom` on Windows, `/dev/urandom` on POSIX).
3. **Constant-Time Verification**: Authentication tags and MACs (AES-GCM and ZIP AE-2 HMAC-SHA1-80) are compared exclusively with `crypto::constant_time_equal()`.
4. **Dual Integrity Checks**: Native `.orar` entries verify both CRC32 and SHA-256 checksums on extraction and test operations.
5. **No Untrusted Shell Interpolation**: External format helpers (`bsdtar`, `unrar`, `7z`) are invoked via explicit argument vectors (`CreateProcessW` on Windows / `execvp` on POSIX) rather than shell command strings.

## Reporting a Vulnerability

If you discover a security vulnerability (such as a path traversal, memory safety bug, or cryptographic flaw):

1. Do **not** open a public issue with exploit details.
2. Report it privately via GitHub Security Advisories ("Report a vulnerability" in the repository Security tab) or contact the maintainers directly.
3. Include a minimal reproduction archive or test case, the affected version, and your OS/compiler.

We aim to acknowledge reports within 48 hours and provide a fix and advisory promptly.
