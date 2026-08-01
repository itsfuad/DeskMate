# Zed project configuration

`settings.json` allows clangd to inspect the PlatformIO cross-compilers. The
include paths and the active default `deskmate` defines live in the root
`.clangd` file so they also work from other clangd clients.

The paths target the local PlatformIO package cache:

- `toolchain-xtensa` — ESP8266 / Xtensa LX106
- `toolchain-xtensa-esp-elf` — ESP32 / Xtensa
- `toolchain-riscv32-esp` — ESP32-C2 / RISC-V

If PlatformIO is installed under a different home directory, update the
absolute paths in `.clangd` and `.zed/settings.json`.
