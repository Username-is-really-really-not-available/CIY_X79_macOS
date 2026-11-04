# Contributing & Compatibility Guidelines

**Languages:** [Deutsch](CONTRIBUTING.md) | [English](CONTRIBUTING_EN.md)

Thank you for your interest in improving the CIY X79 RGB macOS project!

This project follows a quality- and security-oriented approach (**Compatibility Foundation**): Before new keyboard layouts or devices are declared officially compatible, support must be transparent, reproducible, and hardware-verified.

---

## 1. Compatibility Taxonomy

To avoid misunderstandings and unverified compatibility claims, the project uses four required status levels:

* **Maintainer-verified:** The exact model/revision and all features have been physically verified by the maintainer on real hardware under macOS. (Currently: CIY X79 ISO-DE.)
* **Community-verified:** A reproducible hardware test report from a device owner is available, including hardware details, macOS version, CPU architecture, and protocol evidence; maintainer review is still pending.
* **Experimental / unverified:** Support has not been confirmed (for example, theoretical mappings based on firmware or MCU similarities); functionality and reliability are not guaranteed.
* **Unsupported:** Reserved for models or revisions proven incompatible (different firmware, incompatible protocol, or failed hardware tests), not for models that have simply not been tested.

---

## 2. Privacy & Security Rules

1. **No keylogging or input monitoring:** This project never reads keyboard input. It neither needs nor requests Accessibility or Input Monitoring permissions. Contributions that introduce such hooks will be rejected.
2. **Log hygiene:** Remove personal data, usernames, and serial numbers from diagnostic output and logs before uploading them.
3. **Fail closed:** Unknown VID/PID combinations must never trigger write operations.
4. **Offline by default:** The project uses no network connections, telemetry, or cloud services.

---

## 3. Workflow for New Layouts & Hardware

### Step 1: Identify the Hardware (Passive Inspection)
Collect the exact USB descriptors for your device in Terminal:

```bash
# Read USB descriptors passively:
system_profiler SPUSBDataType | grep -A 10 -B 2 -i "0x258a"
```

Record:
* Exact product name and revision (printed on the case or PCB).
* Physical layout (for example, ANSI-US with 68 keys, ISO-UK).
* Vendor ID (VID), Product ID (PID), and USB interface number.

### Step 2: Determine the Layout Model & Key Matrix
The CIY X79 ISO-DE layout uses the fixed mapping rule `led_index = matrix_slot + 1`. **Do not apply this rule blindly to other MCUs or keyboards.**

Use the hardware-free tools to prepare your changes:
```bash
# 1. Validate a profile draft without USB access:
./ciy_ctl validate-profile my_new_layout.json

# 2. Simulate the transfer plan without hardware access (dry run):
./ciy_ctl --dry-run load my_new_layout.json
```

### Step 3: Perform Hardware Write Tests
> [!CAUTION]
> Run write tests (for example, setting individual colors or sweeping keys) only after verifying that the VID/PID and report format (`Report 0x0B` and `Report 0x0A`) match your keyboard.

Test the following:
1. Does the lighting respond to `Report 0x0B` followed by `Report 0x0A` (mode 19)?
2. Are keys illuminated at the expected physical positions?
3. After unplugging and reconnecting the keyboard (hotplug), does it resynchronize as expected?

### Step 4: Submit a Report
Open a GitHub issue using the [Device / Layout Request](.github/ISSUE_TEMPLATE/device_layout_request.md) template.

---

## 4. Code Contribution Guidelines (Pull Requests)

* **Zero-bloat macOS:** Do not add dependencies on Homebrew, Node, Docker, or the full Xcode.app. The project must build directly with `clang` (`-O2`, Universal Binary `arm64`/`x86_64`, `-mmacosx-version-min=12.0`).
* **Separate model and view:** Put layout metadata in `src/layouts/ciy_layout.h` and dedicated source files (`src/layouts/ciy_<model>_<layout>.c`), separating logical keys (`CIYLogicalKey`) from visual key surfaces (`CIYKeySurface`).
* **Test coverage:** All tests in `./scripts/run_tests.sh` must pass with AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`).
* **Universal Binary:** `./scripts/build_app.sh` must complete successfully and support both architectures.
