# 📱 MC75 Reset & Full-Update Guide (over USB)

> How to wipe a Motorola/Symbol/Zebra **MC75 / MC75A** and bring it **as up to
> date as the hardware allows**, driven from a Windows PC over the **USB
> cradle** — then deploy the HomeBox client (`HBXClient.cab`).

---

## 0. First, the headline facts

- **Windows Mobile 6.5 is the ceiling for this hardware.** The MC75A shipped
  with WM6.5; there is **no newer Windows Mobile** for it (Windows Phone 7 was a
  different, incompatible platform that never ran on rugged OEM devices). "Fully
  up to date" therefore means: flash the **last OEM OS/BSP build**, then the
  **last standalone Wi-Fi (Fusion) + hotfix** on top.
- The **latest OS is BSP `04.47.04` (Rev D)** — WEHH 6.5, AKU
  `29217.5.3.12.26`, released **25 Feb 2014** — for the **MC75A0** (HSPA/GSM
  "Premium") and **MC75A8** (WLAN-only "Professional"). Nothing newer exists.
- On top of the OS you then apply two on-device packages: **Wireless Fusion
  `3.00.2.0.039R`** (Apr 2015) and **HotFix CFE `v01.01.00`** (Jan 2016).
- The whole thing is done over USB with **Windows Mobile Device Center (WMDC)**
  — no special flashing tool; the PC just copies files and the device runs its
  own **Update Loader**.

### TL;DR order of operations

```
1.  Identify your exact model + read current versions   (§1)
2.  Set up the PC: WMDC over USB + the connect fix       (§4)
3.  BACK UP anything you need off the device             (§5)
4.  (optional) Factory/clean reset to a known state      (§6)
5.  Flash OS/BSP 04.47.04 via the Update Loader          (§7)   ← the main event
6.  Install Fusion 3.00.2.0.039R, then HotFix CFE        (§8)
7.  Reconfigure (calibrate, region, Wi-Fi, WWAN/APN)     (§9)
8.  Deploy HBXClient.cab (+ make it survive resets)      (§10)
9.  Verify everything                                    (§11)
```

> ⚠️ **Keep the device on AC power (in a powered cradle) during any OS update or
> clean boot.** Losing power mid-flash can brick the unit.

---

## 1. Identify your device and read its current versions

The exact build and package names depend on the model. Check the label / and
on-device **Start → Settings → System → About** (a.k.a. the System Info applet;
the OEM/BSP version also flashes on the boot splash screen):

| Model | Radios | Newest OS/BSP | Notes |
|-------|--------|---------------|-------|
| **MC75A0** (MC75A0-…​) | WLAN + BT + GPS + **HSPA (GSM/UMTS)** | **04.47.04 Rev D** | "Premium"; this guide's primary target |
| **MC75A8** | WLAN + BT + GPS (no WWAN) | **04.47.04 Rev D** | "Professional" (WLAN-only) |
| **MC75A6** | WLAN + BT + GPS + **EVDO/CDMA** | **03.41.03 Rev C** (public) | A 04.47.04 A6 image likely exists but its exact package name is **unverified** |
| **Original MC75** (MC7596/MC7598) | earlier hardware | ships **WM6.1** (`02.35.01`); **WM6.5 upgrade** available | WM6.5 build is delivered as a support-contract **DCP**, build string not public |

Record, from **About**:
- **OEM / BSP version** (e.g. `04.47.04`) and the **Windows Embedded Handheld
  build** (e.g. `29217.5.3.12.26`).
- Whether Wi-Fi is managed by **Fusion** (it is on these units) and its version.

> This guide targets the **MC75A0** on WM6.5. If you have an MC75A6 (EVDO) or the
> original MC75, the *procedure* is identical — only the **package filenames and
> latest build** differ (use your model's packages from Zebra Support).

---

## 2. What "fully up to date" looks like (target end-state)

| Component | Target version | How it's delivered |
|-----------|----------------|--------------------|
| **OS / BSP** (the anchor) | **04.47.04 Rev D** (build `29217.5.3.12.26`) | Update Loader `.zip` (or AirBEAM `.APF`) |
| **Wireless Fusion (Wi-Fi)** | **3.00.2.0.039R** | standalone **CAB** (newer than the `031R` baked into the OS) |
| **WLAN HotFix CFE** | **v01.01.00** | standalone **CAB** (needs Fusion ≥ 037R first) |
| **Bluetooth stack** | SS1 **BTExplorer 2.1.1** (build 27990) **+** Microsoft stack (selectable) | in the OS/BSP |
| **Windows Mobile AKU** | **29217.5.3.12.26** (terminal — no newer exists) | in the OS/BSP |
| **DataWedge** | **3.6.8** (pre-installed) | in the OS/BSP |
| **Scanner / imager drivers** | PixDLL `5.15.13.130`, Decoder1d `5.1.13.1` | in the OS/BSP |
| **WWAN modem firmware** | whatever ships in `04.47.04` | in the OS/BSP (no standalone package exists) |
| **EMDK for C** (dev PC only) | **v2.8** (`EMDK-C-020801`, Oct 2014 — the last one) | a **PC SDK** for Visual Studio 2008, *not* an on-device package |

So on the device you flash **three** things in order: **OS 04.47.04 → Fusion
039R → CFE v01.01.00**. Everything else rides along inside the OS image.

---

## 3. What you need

- The **MC75 + its USB cradle** (single-slot cradle or the snap-on USB cable),
  and the cradle's **power supply** (required during updates).
- A **Windows 7 PC** (32- or 64-bit). Windows 10/11 also works with WMDC but
  needs the same connect fix; Windows 7 is the smoothest.
- **Windows Mobile Device Center 6.1** (see §4).
- The device packages from **Zebra Support** → *Support & Downloads → Mobile
  Computers → MC75A* (links in **Sources**). For an MC75A0 you want:
  - `75A0w65HenUL044704.zip` — the **OS Update Loader** package
  - `Fusion_3.00.2.0.039R_WM65.ARM.CAB` — Wi-Fi update
  - `CFE_MC75A_A0_WM_044704_EN_v010100_e_WB.cab` — WLAN hotfix
  - (optional) a **Clean Boot Package** if you want a true factory wipe (§6)
- **(optional) a blank microSD card** — an alternative to USB for delivering the
  OS package, and the more robust path if the device won't boot.
- To **rebuild the app** with real scanning you also need **EMDK for C v2.8**
  installed on your dev PC — see [WINDOWS7_BUILD.md](WINDOWS7_BUILD.md) and
  [SCANNING.md](SCANNING.md).

---

## 4. Set up USB connectivity (WMDC)

Windows Mobile Device Center is how the PC talks to the device over the cradle.

1. **Enable .NET Framework 3.5** on the PC (*Control Panel → Programs → Turn
   Windows features on or off*). WMDC needs it.
2. Install **WMDC 6.1** (version `6.1.6965`):
   - 64-bit Windows: `drvupdate-amd64.exe`  •  32-bit: `drvupdate-x86.exe`
   - Run **as Administrator**. (Microsoft has retired WMDC from its Download
     Center; get the installer from a trusted archive mirror — see Sources.)
   - If the drivers don't take, re-run the installer **as Administrator in
     Compatibility Mode → Windows Vista**.
3. Dock the MC75 and connect the cradle by USB. WMDC should pop up when the
   device is detected.
4. **Choose a Guest connection.** In the WMDC window pick **"Connect without
   setting up your device"** (guest) rather than creating a partnership — for a
   one-off reset/deploy you only need file access, and guest avoids leaving a
   partnership on the PC.

### 🔧 If WMDC won't connect (the classic Win7/Win10 issue)

Apply **both** fixes and reboot the PC:

**(a) Disable svchost splitting** for the two connectivity services — run an
**Administrator** Command Prompt:

```bat
REG ADD HKLM\SYSTEM\CurrentControlSet\Services\RapiMgr /v SvcHostSplitDisable /t REG_DWORD /d 1 /f
REG ADD HKLM\SYSTEM\CurrentControlSet\Services\WcesComm /v SvcHostSplitDisable /t REG_DWORD /d 1 /f
```

**(b) Fix the service log-on account** — open `services.msc`, and for both
**"Windows Mobile-2003-based device connectivity"** (`WcesComm`) and **"Windows
Mobile-based device connectivity"** (`RapiMgr`):
- **Log On** tab → **This account** → `NT AUTHORITY\LocalService`, blank
  password. *(Some vendor notes use **Local System** instead — both are reported
  to work; try LocalService first.)*
- **General** tab → Startup type **Automatic** → **Start**.
- **Recovery** tab → set failures to **Restart the Service**.

Still nothing? Microsoft's fallback (KB931937): with the device docked, open
`devmgmt.msc`, uninstall **Microsoft Windows Mobile Remote Adapter** (Network
adapters) and **Microsoft USB Sync** (Mobile Devices), then unplug/replug to
force a clean driver reinstall. If large file copies stall, on the device enable
**Settings → Connections → USB to PC → "Enable advanced network
functionality."**

---

## 5. Back up the device first ⚠️

**WMDC has no ActiveSync-style Backup/Restore** — "backup" here means **manual
file copy**. A clean boot / OS flash **wipes the persistent store** (all
settings, the registry, and installed apps), so save anything you need first:

1. In WMDC click **File Management → "Browse the contents of your device."**
2. Copy these to the PC:
   - **`\Application`** — persistent flash; staged apps, `.CPY`/`.REG` files, and
     any config meant to survive a reset. **Always back this up.**
   - Your **user data** (e.g. `\My Documents`, and any folder the app writes to —
     for HBXClient that's `\Program Files\HBXClient\`, incl. `hb_conf.json` and
     the journal file).
   - **`\Storage Card`** if a microSD is fitted (or just remove the card).

**What file-copy does *not* capture:** the live Windows CE **registry** (device
settings, radio/Wi-Fi config) and anything in volatile storage (`\Temp`). Live
registry values only persist across a wipe if they were staged as **`.REG`
files under `\Application`**. For fleet-scale, repeatable backup/provisioning use
Zebra's **MSP / Rapid Deployment / AirBEAM** (the **MSP 3 Client** is
pre-installed on the MC75A) — but for a single device the USB file-copy is fine.

---

## 6. Reset options (know the difference before you wipe)

| Reset | How | What it clears | When to use |
|-------|-----|----------------|-------------|
| **Warm boot** (soft reset) | Hold **Power ~5 s**; release as it starts to boot | RAM only; **keeps** flash + SD | App hung / minor glitch; after installing a CAB |
| **Cold boot** | Hold **Power + `1` + `9`** together; release at the splash | RAM + re-inits drivers, resets clock; **keeps** flash + SD | Warm boot didn't recover it |
| **Clean boot** (factory reset) | Clean Boot Package + boot sequence (below) | **Wipes the entire persistent store** (file system + registry) to factory; **keeps** `\Application` + SD | Start from a known-good factory state before re-provisioning |

### The storage model (what survives what)

| Store | ~Size | Warm | Cold | Clean |
|-------|------:|:----:|:----:|:-----:|
| **RAM** (incl. volatile Cache Disk, `\Temp`) | — | ✗ lost | ✗ lost | ✗ lost |
| **Persistent Storage** (file system + registry, Flash) | ~700 MB | ✓ | ✓ | ✗ **wiped to factory** |
| **`\Application`** (super-persistent Flash) | ~110 MB | ✓ | ✓ | ✓ **retained** |
| **`\Storage Card`** (microSD) | — | ✓ | ✓ | ✓ |

**Persistence mechanism (how staged files auto-restore after a cold/clean
boot):** early in boot, **RegMerge** applies any **`.reg`** files found under
`\Application` into the registry, and **CopyFiles** processes **`.CPY`** files
(format `\Application\src > \dest`) to copy files out to their runtime locations
(WM6 also supports XML `_setup.xml`/CPF provisioning). So whatever you stage
under `\Application` is what repopulates a freshly-wiped device. *(This is the
`\Application` folder mechanism — there is no separate "AppCenter.")*

### Performing a clean boot (factory reset)

1. Download the **Clean Boot Package** for your model from Zebra Support and
   follow the instructions bundled with it (it places files where the Update
   Loader looks — the **SD card root** or the device **`\Temp`** folder).
2. Put the device on **AC power (powered cradle)**.
3. Press **Power + `1` + `9`** together and, **immediately — before the splash
   screen appears — press and hold the right scan/trigger button.**
4. The device runs the Update Loader, updates, and reboots.
5. **Recalibrate** the touchscreen when prompted.

> If you're going straight to an OS flash (§7), you don't strictly need a
> separate clean boot — flashing `04.47.04` already re-lays the OS. Use the clean
> boot when you specifically want a factory-clean starting point.

---

## 7. Flash the latest OS/BSP over USB — the main event

This updates the OS **and** the bundled Fusion baseline, Bluetooth stacks, AKU,
DataWedge, imager drivers, and WWAN firmware in one shot. Target:
**`04.47.04`**.

**Prerequisite:** the device must already be on **v02.37.01 (Rev B) or any
released WM6.5 build**. From there you can jump straight to Rev D — no forced
intermediate. (The original MC75's WM6.1→6.5 upgrade is a separate,
support-contract download; see §1.)

1. On the PC, **download `75A0w65HenUL044704.zip`** (MC75A0) from Zebra Support
   and **unzip it** to a folder. Inside you'll find many `.bin` partition files
   and a **`pkgs.lst`** manifest, plus Zebra's **`MC75Ax Update
   Instructions.doc`**. *(Don't modify the files — the package is a matched set.)*
2. In WMDC, **File Management → Browse the contents of your device**, and **copy
   ALL of the unzipped files into the device `\Temp` folder.**
   - *Alternative (more robust):* copy the same files to the **root of a microSD
     card** and insert it into the device — recommended if `\Temp` space is
     tight or the OS is unstable.
3. **Put the device in its powered cradle (AC power).** Do not remove power for
   the rest of this step.
4. On the device: **Start → File Explorer**, browse to `\Temp` (or the Storage
   Card), and run **`STARTUPDLDR.EXE`**.
5. The Update Loader flashes the image — **~5 minutes** — then reboots itself.
   Do not touch power.
6. **Recalibrate** the screen when it comes back.

> **Data note:** the standard Update Loader run resets the persistent store to
> the new image's defaults but **preserves `\Application`**. (The package also
> includes an optional app-partition clean file, `75A0w65HenCA000001.bin`; it is
> *not* run unless its name is added to `pkgs.lst`.) The **AirBEAM** method
> (`75A0w65HenAB044704.APF` via MSP) erases all data.

**Verify:** **Start → Settings → System → About** should now show **BSP
`04.47.04`** and build **`29217.5.3.12.26`**.

---

## 8. Update the remaining components (two CABs, in order)

Both are simple CAB installs over USB.

1. **Wireless Fusion `3.00.2.0.039R`** (newer than the `031R` inside the OS):
   - Copy **`Fusion_3.00.2.0.039R_WM65.ARM.CAB`** to the device (WMDC file
     browser → e.g. `\Temp` or `\My Documents`).
   - On the device, **tap the CAB** in File Explorer. It installs and
     **auto warm-boots**. (An AirBEAM `..._no_reboot` variant exists for silent
     deployment.)
2. **WLAN HotFix `CFE v01.01.00`** (fixes Wi-Fi reconnect after resume/cradle —
   SPR 28401/26402). **Requires Fusion ≥ 037R first (done in step 1).**
   - Copy **`CFE_MC75A_A0_WM_044704_EN_v010100_e_WB.cab`** to **`\Application`**
     (so it persists), then **tap it**. It installs and warm-boots. (Use the
     `_NB.cab` for silent/MDM, then reboot manually.)

Bluetooth (SS1 BTExplorer 2.1.1 + MS stack), the terminal AKU
`29217.5.3.12.26`, DataWedge 3.6.8, and the imager/decoder drivers all came with
the OS in §7 — nothing separate to install.

---

## 9. Reconfigure the device

After the wipe + flash:

1. **Calibrate** the touchscreen; set **date/time, time zone, region**.
2. **Wi-Fi:** open **Wireless Fusion** (Fusion → *Wireless Companion / Find WLANs*),
   create/import your WLAN **profile** (SSID, security, IP). To make the profile
   survive future cold boots, export/stage it under **`\Application`**.
3. **WWAN (MC75A0):** insert the SIM, set the carrier **APN** under
   **Settings → Connections**, and confirm the phone/data connects.
4. **Bluetooth:** pick the stack you want in the Bluetooth applet (SS1 BTExplorer
   or the Microsoft stack) and pair peripherals.
5. Re-establish the WMDC connection (guest) so you can deploy the app.

---

## 10. Deploy the HomeBox client (`HBXClient.cab`) over USB

Build the CAB per [WINDOWS7_BUILD.md](WINDOWS7_BUILD.md) (the `HBXClientCab`
project produces `bin\Release\HBXClient.CAB`), then:

1. WMDC → **Browse the contents of your device** → copy **`HBXClient.CAB`** to
   the device.
2. On the device, **tap the CAB** in File Explorer. It installs to
   **`\Program Files\HBXClient\`** and warm-boots.
3. Create the runtime config **`\Program Files\HBXClient\hb_conf.json`** (API
   URL, device id, sync interval — see [DEPLOYMENT.md](DEPLOYMENT.md)) by copying
   it over with WMDC.

### Make the app survive a cold/clean boot (recommended)

A normally-installed CAB lands in non-persistent program storage and is **lost on
a cold/clean boot**. To make HBXClient auto-restore, **stage it in
`\Application`**:

- Copy `HBXClient.CAB` to `\Application\`, and add a small **`.CPY`** or an
  auto-install/`_setup.xml` entry so the persistence mechanism re-installs it on
  boot; put `hb_conf.json` under `\Application` too (with a `.CPY` to
  `\Program Files\HBXClient\`). For fleet deployment, wrap it as an **AirBEAM/MSP**
  package instead. See §6 for how `\Application` + `.CPY`/`.REG` persistence works.

> Real barcode scanning requires the CAB to have been built with **EMDK for C
> v2.8** and the `HBX_USE_EMDK` define (on by default) — see
> [SCANNING.md](SCANNING.md). Without the EMDK the app still installs and runs;
> the scanner just falls back to the simulation path.

---

## 11. Final verification checklist

```
[ ] Settings → System → About shows BSP 04.47.04 / build 29217.5.3.12.26
[ ] Wireless Fusion shows 3.00.2.0.039R  (and the CFE hotfix applied)
[ ] Wi-Fi associates and gets an IP; WWAN/data connects (MC75A0)
[ ] Bluetooth applet shows the desired stack; peripherals pair
[ ] HBXClient launches; a trigger pull decodes a barcode into the scan screen
[ ] HBXClient reaches your API (HTTPS) and the queue view works
[ ] Cold-boot test: staged \Application items (app + config + Wi-Fi) auto-restore
```

---

## 12. Troubleshooting

| Symptom | Fix |
|---------|-----|
| WMDC never shows the device | Apply the two connect fixes in §4 (SvcHostSplitDisable keys + service Log-On), reboot the PC, replug the cradle |
| `STARTUPDLDR.EXE` not found / nothing happens | You didn't copy **all** unzipped files (incl. `pkgs.lst`) to `\Temp`/SD; re-copy the complete set |
| Update won't start or reboots early | Device must be on **AC power in the cradle**; free up `\Temp` space or use the **SD card** method |
| Device won't boot to run the loader | Put the package on the **SD card root** and use the SD recovery boot (cold-boot key combo while the card is inserted) |
| Wi-Fi drops after suspend/cradle | Install **Fusion 039R** then **CFE v01.01.00** (§8) |
| App gone after a cold boot | Stage it under **`\Application`** with a `.CPY` / AirBEAM package (§10) |
| Can't find the download links | Zebra Support → *Support & Downloads → Mobile Computers → MC75A* (Sources). Pages load file lists via JavaScript; the **release-notes PDFs** carry the exact package filenames |

---

## 13. Model / version caveats (verify against your unit)

- **MC75A6 (EVDO):** the newest **publicly documented** OS is **`03.41.03`**; a
  `04.47.04` A6 image likely exists but its exact package filename is
  **unverified** here — get it from Zebra Support for your model.
- **Original MC75 (MC7596/MC7598):** ships **WM6.1** (`02.35.01`); the **WM6.5
  upgrade** is a support-contract **DCP** (the exact WM6.5 build string isn't
  published). Same Update Loader mechanism.
- **`04.47.05`** appears in some Fusion compatibility tables, but only
  **`04.47.04` (Rev D)** has published release notes as the public latest OS —
  treat `04.47.05` as unconfirmed as a downloadable image.
- **WWAN modem firmware** has no standalone package/version — it's whatever ships
  inside the OS image.
- Exact **key combos** and the **clean-boot package** contents are from the
  official **MC75A Integrator Guide**; confirm against the guide/package for your
  specific unit.

---

## Sources

- MC75A support & downloads (OS, Fusion, packages): https://www.zebra.com/us/en/support-downloads/mobile-computers/handheld/mc75a.html
- MC75 support & downloads: https://www.zebra.com/us/en/support-downloads/mobile-computers/handheld/mc75.html
- MC75A0 Premium **BSP 04.47.04** release notes (PDF): https://www.zebra.com/content/dam/support-dam/en/documentation/unrestricted/release-notes/mc75a-operating-system-bsp-04-47-04-release-notes2.pdf
- MC75A8 Professional BSP 04.47.04 release notes: https://www.zebra.com/us/en/support-downloads/software/release-notes/operating-system/mc75a-operating-system-bsp-04-47-04-release-notes.html
- MC75A **HotFix CFE v01.01.00** release notes: https://www.zebra.com/us/en/support-downloads/software/release-notes/operating-system/mc75a-operating-system-bsp-v04-47-04-hotfix-cfe-v01-01-00-release-notes.html
- MC75A6 (EVDO) BSP 03.41.03 release notes: https://www.zebra.com/us/en/support-downloads/software/release-notes/operating-system/mc75a-operating-system-bsp-03-41-03-release-notes.html
- Original MC75x6 OS 02.35.01 (WM6.1) release notes (PDF): https://www.zebra.com/content/dam/support-dam/en/documentation/unrestricted/release-notes/release-notes-MC75x6-OS02-35-01.pdf
- **Wireless Fusion** downloads: https://www.zebra.com/us/en/support-downloads/software/utilities/fusion.html
- Fusion **3.00.2.0.039R** for WM6.5 release notes (PDF): https://www.zebra.com/content/dam/zebra_new_ia/en-us/software/utilities/fusion/Fusion-3-00-2-0-039R-For-Windows-Mobile-6-5-Release-Notes.pdf
- **EMDK for C v2.8** (build-time SDK for VS2008): https://www.zebra.com/us/en/support-downloads/software/developer-tools/emdk-for-c.html
- MC75A **Integrator Guide** (reset combos, clean boot, storage/persistence model): https://www.mobileidsolutions.com/resources/product-info/motorola/mc75a-integrator-guide_13362402a.pdf
- MC75A User Guide (warm/cold boot): https://automation.bartec.de/DataRoot/mobile/MC75Axex-NI/manuals/zebra/EN_MC75A_User_Guide.pdf
- Microsoft — WMDC description + "device will not connect" (KB931937): https://learn.microsoft.com/en-us/previous-versions/troubleshoot/windows/win32/description-of-windows-mobile-device-center
- WMDC 6.1 installers (archive mirror): https://archive.org/details/MicrosoftWindowsMobileDeviceCenter
- WMDC connect fix (SvcHostSplitDisable + service log-on): https://neigps.com/news/windows-mobile-device-center-connection-issues/ · https://www.junipersys.com/support/article/13929
- `\Application` cold-boot persistence (`.CPY`/`.REG`): https://www.mobilitysolutions.cz/eng/faq/mobile-computers/cold-boot-persistence
- Factory resets for Zebra WinCE/WM devices: https://www.mobilitysolutions.cz/eng/faq/mobile-computers/factory-resets

---

*Version numbers and package names above were current as of this writing and
verified against Zebra release-notes PDFs; always cross-check the download for
your exact model on Zebra Support. Items flagged “unverified” in §13 could not be
confirmed from a primary Zebra source.*

[← Back to README](../README.md) · [Build the app →](WINDOWS7_BUILD.md) · [Scanning/EMDK →](SCANNING.md)
