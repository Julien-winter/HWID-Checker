# HWID Checker

A lightweight Windows tool that collects hardware IDs from your system and displays them in a clean format. Useful for system identification, anti-cheat verification, or hardware tracking.

## Features

- Collects 6 hardware identifiers:
  - **Motherboard Serial**
  - **CPU ID**
  - **Disk Serial(s)** — supports multiple disks
  - **BIOS Serial** — filters out generic/OEM placeholders
  - **MAC Address(es)** — shows all active network adapters
  - **System UUID**
- **WMIC + PowerShell fallback** — works on Windows 10/11 (even without WMIC)
- **Auto-elevation** — restarts as Administrator automatically
- **Clipboard copy** — copy results directly to clipboard
- **Export** — JSON or CSV output
- **Auto-updater** — checks GitHub for new releases
- **Headless mode** — for scripting and automation

## Usage

```
HWIDChecker.exe [options]
```

### Options

| Flag | Description |
|------|-------------|
| `--help` | Show help message |
| `--version` | Show version |
| `--quiet` | Only output HWID values (no formatting) |
| `--headless` | Skip prompts, useful for scripts |
| `--no-update` | Skip auto-update check |
| `--copy` | Copy results to clipboard |
| `--export FILE` | Export results to file (.json or .csv) |

### Examples

```bash
# Basic usage
HWIDChecker.exe

# Copy to clipboard
HWIDChecker.exe --copy

# Export as JSON
HWIDChecker.exe --export results.json

# Export as CSV
HWIDChecker.exe --export results.csv

# Headless + copy (for scripts)
HWIDChecker.exe --headless --copy --no-update
```

## Requirements

- Windows 10 or Windows 11
- Administrator privileges (auto-elevates if not running as admin)

## How It Works

1. **WMIC** — tries to query hardware info via Windows Management Instrumentation Command-line
2. **PowerShell fallback** — if WMIC is unavailable (common on Windows 11), uses PowerShell `Get-WmiObject` cmdlets
3. **BIOS filtering** — generic placeholders like "O.E.M.", "To Be Filled", "Default" are filtered out
4. **Output** — results are displayed in a colored table and optionally copied/exported

## Download

Download the latest release from [Releases](https://github.com/Julien-winter/HWID-Checker/releases/latest).

## Building

Requires Visual Studio 2022 with C++ Desktop Development workload.

```
msbuild "HWID Checker.sln" /p:Configuration=Release /p:Platform=x64
```

## License

GPL v3
