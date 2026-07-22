# 🪟 Building MC75 HomeBox Client on Windows 7 — From Scratch

> A complete, step‑by‑step guide to setting up a Windows 7 machine and compiling
> **HBXClient.exe** (and its CAB installer) for the Motorola MC75
> (Windows Mobile 6.5 Professional, ARMV4I).
>
> Follow the sections **in order** — install order matters, and there is one
> project‑file gotcha (§6) that trips up almost everyone.

---

## 0. TL;DR checklist

If you already know the toolchain, here is the whole thing on one screen:

```
[ ] Windows 7 (32‑ or 64‑bit), admin account
[ ] Visual Studio 2008 Professional              (install FIRST)
[ ] Visual Studio 2008 SP1                        (KB945140)
[ ] Windows Mobile 6 Professional SDK (Refresh)   (after VS2008)
[ ] Windows Mobile 6.5 Developer Tool Kit (DTK)   (after the WM6 SDK) — recommended
[ ] Windows Mobile Device Center 6.1              (only to deploy/debug on a device)
[ ] Zebra EMDK for C                              (OPTIONAL — not needed to compile today)
[ ] git clone the repo
[ ] Retarget the solution platform to the SDK you installed   ← the #1 gotcha (§6)
[ ] Build → HBXClient (Debug or Release), then HBXClientCab
```

The current code compiles and links against **only** the stock Windows CE
libraries (`coredll`, `aygshell`, `commctrl`, `ole32`, `oleaut32`, `winsock`).
The scanner layer (`ScannerHAL.cpp`) is a simulation stub — every EMDK call is
commented out — so **you do not need the Zebra EMDK just to compile**.

---

## 1. What you are building (and why Visual Studio 2008)

| Item | Value |
|------|-------|
| Output | `HBXClient.exe` — a **native** (unmanaged C++) Windows CE executable |
| CPU | **ARMV4I** (32‑bit ARM) — a cross‑compile; it will not run on the PC |
| OS target | Windows Mobile 6.5 Professional (Windows CE 6, `_WIN32_WCE=0x0600`) |
| Also produced | `HBXClient.cab` — the on‑device installer (`HBXClientCab` project) |

> ### ⚠️ You must use Visual Studio **2008**
> Windows Mobile / "Smart Device" C++ projects are only supported by
> **Visual Studio 2005 and 2008**. Visual Studio 2010 and newer **removed**
> Smart Device / Windows Mobile support entirely and cannot open or build this
> `.vcproj`. VS2008 is non‑negotiable for the on‑device build.
>
> (If you only want to exercise/verify the *logic* on a modern machine without
> any of this, see **Appendix B** — the repo ships a host test harness that
> builds with any g++/clang++. It does **not** produce a device binary.)

---

## 2. System requirements

- **Windows 7** SP1, 32‑bit or 64‑bit. (Everything below also works on Vista;
  on Windows 8/10/11 VS2008 is unsupported and flaky — stick to Win7.)
- **Administrator** account (all installers need it).
- **~10 GB** free disk (VS2008 + SDKs + DTK emulators).
- **4 GB** RAM recommended.
- On **64‑bit** Windows 7, VS2008 and the SDKs install under
  `C:\Program Files (x86)\` — that is normal.

> 💡 Do everything from an account with admin rights, and **right‑click →
> "Run as administrator"** on each installer. If an old installer misbehaves,
> right‑click → **Properties → Compatibility → Windows XP (SP3)**.

---

## 3. Software you will install

| # | Software | Typical file / package | Where |
|---|----------|------------------------|-------|
| 1 | **Visual Studio 2008 Professional** | ISO / setup.exe | MSDN subscription or your existing VS2008 media (Microsoft no longer sells it) |
| 2 | **Visual Studio 2008 SP1** | `VS90sp1-KB945140-ENU.exe` | Microsoft Download Center |
| 3 | **Windows Mobile 6 Professional SDK (Refresh)** | `Windows Mobile 6 Professional SDK (Refresh).msi` | Microsoft Download Center |
| 4 | **Windows Mobile 6.5 Developer Tool Kit** (recommended) | `WM65DTK.msi` (6.5) / `WM653_DTK.msi` (6.5.3) | Microsoft Download Center |
| 5 | **Windows Mobile Device Center 6.1** (deploy/debug only) | `drvupdate-x86.exe` / `drvupdate-amd64.exe` | Microsoft Download Center |
| 6 | **Zebra EMDK for C** (OPTIONAL, real scanner only) | EMDK for C installer | Zebra support/developer portal (free account) |
| 7 | **Git for Windows** (to clone) | `Git-*-64-bit.exe` | git‑scm.com (or download the repo ZIP instead) |

> These are **legacy** downloads. VS2008 itself requires a valid license /
> MSDN subscription. The SDKs, DTK and WMDC are archival items on the Microsoft
> Download Center — search the Download Center by the names above. The Zebra
> EMDK is on Zebra's support portal and needs a (free) account.

---

## 4. Installation — do these in order ⬇️

**The order matters.** The Windows Mobile SDK/DTK detect Visual Studio during
setup and only integrate into an IDE that is already installed. If you install
an SDK *before* VS2008, it will not wire itself into VS and no Smart Device
platform will appear.

### Step 4.1 — Visual Studio 2008 Professional

1. Mount/extract the VS2008 media, right‑click `setup.exe` → **Run as administrator**.
2. Choose **Custom** and make sure **Visual C++** (and, if offered, "Smart
   Device Programmability" / "Smart Device CAB Project") is selected.
3. Finish and **reboot**.

### Step 4.2 — Visual Studio 2008 SP1

1. Run `VS90sp1-KB945140-ENU.exe` as administrator.
2. This is **required** for Windows 7 stability. Reboot when done.

### Step 4.3 — Windows Mobile 6 Professional SDK (Refresh)

1. Run the WM6 Professional SDK `.msi` as administrator.
2. A **Complete** install adds the ARMV4I headers/libs, the Device Emulator,
   and registers the build platform **"Windows Mobile 6 Professional SDK
   (ARMV4I)"** inside VS2008.
3. This SDK provides everything this project links against.

> The WM6 **Professional** SDK is the one you want (Professional = touch/PPC
> devices like the MC75). The "Standard" SDK is for non‑touch phones.

### Step 4.4 — Windows Mobile 6.5 Developer Tool Kit (recommended)

1. Run `WM65DTK.msi` (or the 6.5.3 DTK) as administrator.
2. It installs **on top of** the WM6 Professional SDK and adds the Windows
   Mobile 6.5 emulator images and the 6.5 gesture/UI headers, plus a platform
   named like **"Windows Mobile 6.5.3 Professional DTK (ARMV4I)"**.
3. This is optional for *compiling*, but you want the **6.5 emulator** to run
   the app without a physical MC75.

### Step 4.5 — Windows Mobile Device Center 6.1 (only for device deploy/debug)

1. Run `drvupdate-amd64.exe` (64‑bit Win7) or `drvupdate-x86.exe` (32‑bit).
2. WMDC replaces ActiveSync on Windows 7 and is how VS2008 talks to a tethered
   MC75 for on‑device deploy/debug. **Not needed to compile.**

### Step 4.6 — Zebra EMDK for C  *(optional — skip for now)*

You only need this when you replace the simulated `ScannerHAL.cpp` with real
barcode scanning. The current code has **no** EMDK includes or libraries, so
skip it for a first successful build. See **§7** if you install it later.

---

## 5. Get the source

Using Git for Windows (Git Bash or the regular Command Prompt):

```bat
cd C:\dev
git clone https://github.com/ethanbissbort/mc75-homebox-client.git
cd mc75-homebox-client
git checkout claude/multi-agent-codebase-build-b7h8qr   REM or main
```

No network drive / spaces‑in‑path surprises: a short path like `C:\dev\...`
avoids the WinCE toolchain's occasional long‑path issues.

Key files:

```
mc75-homebox-client.sln          ← open THIS in Visual Studio 2008
proj\HBXClient.vcproj            ← the executable project
proj\HBXClientCab.vcproj         ← the CAB installer project
```

---

## 6. ⭐ Retarget the solution platform (the #1 gotcha)

The solution and both project files are pinned to a **custom** platform name:

```
Windows Mobile 6.5 Professional SDK (ARMV4I)
```

That exact string is **not** what a stock SDK/DTK install registers. Depending
on what you installed, Visual Studio actually has a platform named:

- `Windows Mobile 6 Professional SDK (ARMV4I)`         ← from the WM6 Pro SDK (§4.3)
- `Windows Mobile 6.5.3 Professional DTK (ARMV4I)`     ← from the WM6.5 DTK  (§4.4)

So when you open the solution, VS2008 will report the platform is **not
installed** and refuse to build until you point the projects at a platform you
actually have. Do **one** of the following.

### Option A (most reliable): edit the files *before* opening

Because VS may mark an unknown‑platform project as "unavailable" (and then you
cannot retarget it from the GUI), the surest route is a find‑and‑replace in
three text files **before** opening the solution.

Replace **every** occurrence of

```
Windows Mobile 6.5 Professional SDK (ARMV4I)
```

with the platform you installed, e.g.

```
Windows Mobile 6 Professional SDK (ARMV4I)
```

in these files:

```
mc75-homebox-client.sln
proj\HBXClient.vcproj
proj\HBXClientCab.vcproj
```

PowerShell one‑liner (run from the repo root; change the target name to match
your SDK):

```powershell
$from = 'Windows Mobile 6.5 Professional SDK (ARMV4I)'
$to   = 'Windows Mobile 6 Professional SDK (ARMV4I)'
Get-ChildItem -Path .\mc75-homebox-client.sln, .\proj\HBXClient.vcproj, .\proj\HBXClientCab.vcproj |
  ForEach-Object {
    (Get-Content $_.FullName -Raw).Replace($from, $to) | Set-Content $_.FullName -NoNewline
  }
```

Now open `mc75-homebox-client.sln` in VS2008 — the platform will resolve.

> Keep the `(ARMV4I)` architecture — the MC75 is ARM. Do not switch to an x86
> emulator platform for the *device* build.

### Option B (GUI): retarget with Configuration Manager

If VS2008 does manage to load the projects:

1. **Build → Configuration Manager**.
2. Under **Active solution platform**, pick **`<New…>`**.
3. In *New Solution Platform*, choose your installed platform (e.g.
   *Windows Mobile 6 Professional SDK (ARMV4I)*) and, for **"Copy settings
   from"**, select the pinned `Windows Mobile 6.5 …` entry so the Debug/Release
   settings carry over. Tick **"Create new project platforms"**.
4. Do this for both **HBXClient** and **HBXClientCab**, then remove the old
   unavailable platform if you like.

---

## 7. (Optional) Clean up the hard‑coded include / library paths

`proj\HBXClient.vcproj` lists some **absolute** search paths that were written
for a specific machine:

```
AdditionalIncludeDirectories =
    ..\include ;
    C:\Program Files\Windows Mobile 6.5 SDK\PocketPC\Include\Armv4i ;
    C:\Program Files\Zebra EMDK\C\Include

AdditionalLibraryDirectories =
    C:\Program Files\Windows Mobile 6.5 SDK\PocketPC\Lib\Armv4i ;
    C:\Program Files\Zebra EMDK\C\Lib\ARMV4I
```

What you need to know:

- **`..\include` is essential** — it is where the project's own headers live.
  Leave it.
- The two absolute **Windows Mobile** paths are **redundant**. When you select
  a Smart Device platform, VS2008 automatically adds that SDK's real system
  include/lib folders. If those hard‑coded folders do not exist on your machine
  VS just emits a *"cannot find directory"* **warning** and builds anyway using
  the platform's own paths.
- The **Zebra EMDK** paths only matter if you install the EMDK (they will
  otherwise warn and be ignored). No EMDK `.lib` is in the linker input, so a
  missing EMDK folder cannot cause a link error.

**Recommended:** to silence the warnings, edit the project (Project → Properties
→ *C/C++ → General → Additional Include Directories* and *Linker → General →
Additional Library Directories*) and reduce them to just `..\include` (plus your
real EMDK paths if/when you add scanning). This is cosmetic — the build succeeds
either way once §6 is done.

If you *do* use the EMDK, point these at your real install, e.g.
`C:\Program Files (x86)\Motorola EMDK for C\...\Include` and the matching
`...\Lib\ARMV4I`, and add the appropriate `.lib` (e.g. `ScanAPI.lib`) under
*Linker → Input → Additional Dependencies*.

---

## 8. Build

### Method 1 — Visual Studio IDE

1. **File → Open → Project/Solution →** `mc75-homebox-client.sln`.
2. Set the configuration bar to **Debug** (or **Release**) and the platform to
   your retargeted **… (ARMV4I)** platform.
3. In **Solution Explorer**, right‑click **HBXClient → Build**.
   - Output: `bin\Debug\HBXClient.exe` (or `bin\Release\HBXClient.exe`).
4. Then right‑click **HBXClientCab → Build** to produce the installer.
   - Output: `bin\Debug\HBXClient_Debug.CAB` or `bin\Release\HBXClient.CAB`.
   - (HBXClientCab depends on HBXClient, so **Build Solution** (F7) builds both
     in the right order.)

### Method 2 — Command line

Open the **"Visual Studio 2008 Command Prompt"** (Start → Microsoft Visual
Studio 2008 → Visual Studio Tools) so `devenv`/`msbuild` are on `PATH`, then:

```bat
cd C:\dev\mc75-homebox-client

REM Build the whole solution (exe + CAB), Release:
devenv mc75-homebox-client.sln /Build "Release|Windows Mobile 6 Professional SDK (ARMV4I)"

REM Or just the exe, Debug:
devenv proj\HBXClient.vcproj /Build "Debug|Windows Mobile 6 Professional SDK (ARMV4I)"
```

> Use the **exact** platform string you retargeted to in §6, including the
> `(ARMV4I)` suffix and the quotes. `devenv` is the most reliable driver for
> VS2008 Smart Device solutions (the bundled `msbuild` can build the `.vcproj`
> too, but `devenv` handles the Smart Device CAB project cleanly).

### Build configurations at a glance

| | Debug | Release |
|---|-------|---------|
| Output dir | `bin\Debug\` | `bin\Release\` |
| Optimization | Disabled (`/Od`) | Maximize Speed (`/O2`, `/Ob2`) |
| Runtime lib | Multithreaded Debug DLL (`/MDd`) | Multithreaded DLL (`/MD`) |
| Debug info | Yes | No |

---

## 9. Verify the build

A successful build shows, in the **Output** window:

```
========== Build: 2 succeeded, 0 failed, 0 up-to-date, 0 skipped ==========
```

and on disk:

```
bin\Release\HBXClient.exe        (~200–300 KB, ARMV4I PE image)
bin\Release\HBXClient.CAB        (the installer)
```

You can confirm the CPU target with `dumpbin` from the VS2008 command prompt:

```bat
dumpbin /headers bin\Release\HBXClient.exe | find "machine"
REM -> should report an ARM (Thumb) machine, not x86
```

---

## 10. Run it

### Option A — Windows Mobile 6.5 emulator (no device needed)

1. **Tools → Device Emulator Manager** in VS2008.
2. Pick a **Windows Mobile 6.5 Professional** emulator image (installed by the
   DTK in §4.4), right‑click → **Connect**, then **Cradle** it (this shares the
   PC network via WMDC so the app can reach your API).
3. Set **HBXClient** as the startup project and press **F5** — VS deploys the
   `.exe` to the emulator and launches it, or manually deploy the `.CAB`.

### Option B — real Motorola MC75

1. Install **Windows Mobile Device Center 6.1** (§4.5) and connect the MC75 via
   USB; accept the partnership prompt.
2. With the device online, in VS2008 choose the device as the deployment target
   and press **F5**, **or** copy `bin\Release\HBXClient.CAB` to the device and
   tap it to install (installs to `\Program Files\HBXClient\`).
3. Create the runtime config `\Program Files\HBXClient\hb_conf.json` on the
   device (see `docs/DEPLOYMENT.md`) so the app knows your API endpoint.

> The real barcode scanner will not fire until `ScannerHAL.cpp` is backed by the
> Zebra EMDK (today it is a simulation stub) — but the UI, HTTP, journal and
> offline‑sync logic all run.

---

## 11. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| *"The project platform 'Windows Mobile 6.5 Professional SDK (ARMV4I)' is not installed"* / project shows **(unavailable)** | The pinned platform name does not match any installed SDK | Do **§6** — find/replace the platform string to your installed platform, or retarget via Configuration Manager |
| No Windows Mobile platform appears at all in VS2008 | An SDK was installed **before** VS2008, or VS2008 SP1 is missing | Install order must be VS2008 → SP1 → SDK → DTK (§4). Re‑run the SDK/DTK installer to re‑integrate |
| `warning: cannot open include directory 'C:\Program Files\Windows Mobile 6.5 SDK\...'` or `...\Zebra EMDK\...` | Hard‑coded absolute paths that do not exist on your PC | Harmless — the build still uses the platform's real paths. Optionally trim them to `..\include` (§7) |
| `fatal error C1083: Cannot open include file: 'windows.h'` | No Smart Device platform selected / wrong platform | Confirm the active platform is a real installed **… (ARMV4I)** platform (§6), not "Win32" |
| `error LNK2019: unresolved external symbol …` referencing `SCAN_*` | You started using EMDK calls without the EMDK lib | Install the Zebra EMDK (§4.6) and add its include/lib paths and `ScanAPI.lib` (§7). Not applicable to the stock code |
| `warning C4819: file contains a character that cannot be represented…` | Source code page vs. file encoding | Cosmetic. If desired, add `/utf-8` (or save the file as the project's Windows‑1252 code page) |
| `fatal error C1060: compiler is out of heap space` | Large TU on 32‑bit toolchain | Add `/Zm200` under *C/C++ → Command Line → Additional Options* |
| Device won't connect / F5 deploy fails | WMDC not installed or partnership not established | Install **WMDC 6.1** (§4.5), reconnect USB, accept the partnership; retry deploy |
| Installers fail on Windows 7 | UAC / old installer | Right‑click → **Run as administrator**; if needed set **Compatibility → Windows XP SP3** |

---

## Appendix A — exact strings & paths (quick reference)

```
Solution to open:      mc75-homebox-client.sln
Executable project:    proj\HBXClient.vcproj      -> bin\{Debug|Release}\HBXClient.exe
CAB project:           proj\HBXClientCab.vcproj   -> bin\{Debug|Release}\HBXClient(.|_Debug.)CAB
Project's own headers: ..\include   (relative to proj\)  — required include dir
Pinned platform name:  Windows Mobile 6.5 Professional SDK (ARMV4I)   (custom — retarget it)
Stock platform names:  Windows Mobile 6 Professional SDK (ARMV4I)
                       Windows Mobile 6.5.3 Professional DTK (ARMV4I)
Preprocessor defines:  WIN32; _WIN32_WCE=0x0600; UNDER_CE; WIN32_PLATFORM_PSPC; (_DEBUG|NDEBUG)
Linked libraries:      coredll.lib aygshell.lib commctrl.lib ole32.lib oleaut32.lib winsock.lib
On‑device install dir: \Program Files\HBXClient\
Runtime config file:   \Program Files\HBXClient\hb_conf.json
```

## Appendix B — fast logic check without any of this

You do **not** need Windows, VS2008 or the SDK to sanity‑check the
platform‑independent core (JSON, models, journal, config, HTTP URL parsing,
API request gating). The repo ships a host harness that builds with any modern
`g++`/`clang++`:

```bash
./scripts/build_host_debug.sh
# compile-checks all 15 source files against a Win32/CE shim, then runs the
# unit + integration suite:  ==== 32/32 test cases passed, 204/204 checks passed ====
```

This is for quick verification on any machine (Linux/macOS/WSL). It does **not**
produce an MC75 binary — for that you need the Windows 7 + VS2008 flow above.
See [BUILD.md → Host Build & Testing](BUILD.md#-host-build--testing).

---

<div align="center">

**🎉 That's the whole toolchain.** Install in order, retarget the platform (§6),
build **HBXClient** then **HBXClientCab**, and deploy the CAB to your MC75.

[← Back to BUILD.md](BUILD.md) · [Deployment →](DEPLOYMENT.md)

</div>
