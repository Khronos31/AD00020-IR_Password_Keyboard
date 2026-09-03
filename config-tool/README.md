# AD00020 IR Password Keyboard Configuration Tool

This directory contains the Rust TUI configuration tool planned for v0.3.0.
It uses arrow keys to move the `>` cursor and Enter to choose. `Ctrl+C`
discards the staged session and exits; `Ctrl+D` is never a save operation.

The current scanner and apply implementation are deliberately placeholders:

- scanning reads one line from standard input and removes only its line ending;
- applying writes public keycodes, aliases, and fixed redaction metadata to
  `build/ad00020-tui-config.placeholder.json`;
- no password plaintext, password-derived value, password length, or reversible
  placeholder is written;
- no libusb, Win32 API, IOKit, or hidapi transport is included yet.

Password-bearing values use `zeroize::Zeroizing` and are not Clone. The backend
interface borrows the configuration, so a future device backend can consume
secret bytes without introducing a clone-based secret-handling path.

## Compatibility

This is v0.3 future tooling and presents 16 password slots. The current v0.2
AD00020 firmware and database format support 12 slots. This TUI does not yet
provision the v0.2 firmware; do not treat its placeholder JSON as a device
configuration file.

## Build and test

From this directory:

```sh
cargo run
cargo test
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
```

Before distributing binaries, inventory all direct and transitive dependency
licenses and include the required notices in the release package. A green
build is not a substitute for that release gate.
