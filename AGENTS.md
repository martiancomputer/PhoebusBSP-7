# Agent guide: PhoebusBSP-7

This repository is the mainline Linux board-support port for the RTL9607C/Cv2. PhoebusBSP-6 is the separate 6.18 LTS/reference port; `sdk/` is the shared Phoebus-SDK Git submodule. Keep mainline API adaptations here, shared vendor/userspace fixes in the SDK, and backports to BSP-6 deliberate and separately verified.

## Where things live

- `build.sh` fetches/caches pristine upstream Linux, grafts the SDK vendor snapshot, applies `overlay/`, builds userspace and kernel, and packages `images/uImage`.
- `configs/rtl9607c.config` is the tracked target kernel configuration.
- `overlay/` contains version-specific platform, NIC/switch, FleetConntrack, PCIe, and Wi-Fi adaptations. `patches/` contains additional kernel patches.
- `tools/build-userspace.sh` owns BSP-7 third-party network userspace; other `tools/` scripts support porting and builds. Review any new tool for embedded machine identifiers before tracking it.
- `sdk/` is a pinned submodule, not a copied source tree. Commit shared changes to the SDK first, publish them, then update this gitlink.
- `build/`, `toolchain/`, and `images/` are generated or downloaded and must not be committed. `SKIP_USERSPACE=1` is for kernel iteration, not a complete router image.

Use `git submodule update --init` and `./build.sh` for a normal build. A successful build does not establish safe module unload or long-term runtime stability; report hardware tests separately. Preserve unrelated or untracked user work.

## Privacy and publication rules

Treat local notes, captures, dumps, temporary debugging files, build outputs, and local configuration as private by default. Ignore those specific files or patterns; do not blanket-ignore public documentation such as `README.md` or `PROJECT.md`.

Never commit passwords, tokens, keys, Wi-Fi credentials, cookies, authentication headers, `.env` files, private dumps, or credential-bearing configuration. Redact personal usernames, emails, hostnames, account IDs, absolute home/workspace paths, device serials, and unnecessary identifying MAC/IP values. Use `$HOME`, `<user>`, `<host>`, `<device>`, or `<account-id>` for examples. Keep subnet/protocol addresses when exact values are necessary to the design and have been reviewed; privacy work must not silently break networking scripts.

Before every commit, inspect `git diff --cached`, run `git diff --cached --check`, and scan staged additions and author/committer metadata for private values. Before every push, inspect the entire outgoing range and any new SDK pin, and confirm the pinned SDK commit is on its remote. If sensitive data is in history, a new redaction commit is insufficient: stop and coordinate history rewriting and any credential rotation. Force-push only when explicitly authorized, against a verified ref. Never put raw local logs, paths, or identifiers into public documentation or commit messages.

Use ignored local configuration or a secret manager for runtime secrets. Ask the user when a value might be technically necessary but its publication is uncertain.
