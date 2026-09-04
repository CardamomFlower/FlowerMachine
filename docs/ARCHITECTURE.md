# FlowerMachine — Architecture (v1)

**Target:** JUCE 8.0.12 standalone GUI app, C++17, Projucer + Visual Studio 2022
(no CMake), Windows x64. Namespace `flowermachine`.
**Status:** LOCKED (v1.0, approved by the owner 2026-09-03). Changes require
discussion and a new explicit approval. Decisions in §9.
**Deployment target:** the owner's work PC, an older Windows 10 of unknown
release, currently not reachable (§7).
**Visual design:** done separately in a dedicated `/design` session. This
document describes the UI functionally only, and keeps all drawing inside
`UI/` so the design pass touches nothing else (§6).

---

## 1. What FlowerMachine is

A cart wall driven by the mouse. An 8×8 grid of carts, each playing one audio
file from the owner's libraries. Pages as tabs. A **preset** is the set of
pages with their assignments, saved to a file.

```
open preset → the visible page decodes to RAM → click a cart → plays from the start
click it again → restarts → remaining time on the cart → its Stop button, or STOP ALL
```

**A cart** (owner's definition): a title (assigned, or the file name), a
Stop button, a Loop toggle, and a gain control up to +18 dB. Nothing else.

It plays files as they are, apart from that gain. **Not in v1** (owner's
decisions, §9): fades, exclusive/solo playback, hotkeys, an edit lock, touch,
MIDI, streaming from disk.

Constants (`Source/Constants.h`):

| Constant | Value | Notes |
|---|---|---|
| `GRID_COLS`, `GRID_ROWS` | 8, 8 | `CARTS_PER_PAGE = 64`. |
| `MAX_PAGES` | 16 | `MAX_CARTS = 1024` sizes the engine's static tables. |
| `MAX_VOICES` | 64 | Simultaneous playing carts. |
| `DECLICK_MS` | 5 | Ramp on start, restart and stop. An implementation detail, not a feature. |
| `CART_GAIN_DB_MIN`, `CART_GAIN_DB_MAX` | −24, +18 | Cart gain range; default 0 dB. |
| `MAX_CART_MINUTES` | 30 | Longest file a cart accepts; longer ones fail with a clear error instead of eating RAM. |
| `COMMAND_FIFO_SIZE` | 256 | Message → audio queue. |
| `UI_REFRESH_HZ` | 30 | Cart timers and progress. |
| `LOADER_THREADS` | clamp(cores − 1, 1, 4) | Decode pool. |
| `PRESET_SCHEMA_VERSION` | 1 | |

---

## 2. Repo layout

```
FlowerMachine/                    (folder currently named JingleMachine)
├── FlowerMachine.jucer           GUI Application; VS2022 exporter; static runtime (§7)
├── CLAUDE.md                     written when this document is locked
├── docs/ARCHITECTURE.md          this file
└── Source/
    ├── Main.cpp                  JUCEApplication, MainWindow, settings, renderer switch
    ├── Constants.h
    ├── Model/
    │   ├── Ids.h                 every juce::Identifier of the preset schema
    │   └── Preset.h/.cpp         ValueTree document: accessors, load/save, path resolution (§3)
    ├── Engine/
    │   ├── SampleData.h          immutable decoded audio (§4)
    │   ├── SampleSlot.h          Svarog's lock-free shared_ptr handoff, copied (§4)
    │   ├── Commands.h            Command POD + AbstractFifo wrapper (§4)
    │   ├── CartEngine.h/.cpp     the AudioSource: voices, mixing, per-cart atomics (§4)
    │   └── AudioEngine.h/.cpp    AudioDeviceManager + AudioSourcePlayer + test tone (§4)
    ├── Loading/
    │   └── SampleLoader.h/.cpp   ThreadPool decode → resample → hand to message thread (§5)
    ├── Control/
    │   └── Controller.h/.cpp     message-thread glue: model ↔ loader ↔ engine (§5)
    └── UI/
        ├── MainComponent.h/.cpp  layout, top bar, status line, UI timer
        ├── PageStrip.h/.cpp      tabs + "+"
        ├── CartGrid.h/.cpp       8×8 of CartButton, drag & drop target
        ├── CartButton.h/.cpp     the cart: play area + Stop, Loop, Gain child controls
        ├── SettingsDialog.h/.cpp device selector, renderer, test tone, About info
        └── FlowerLookAndFeel.h/.cpp  colours/fonts/shapes for everything else
```

Dependency direction: `UI → Control → (Engine, Loading, Model)`. The engine
knows cart ids, not the model. The model knows nothing about audio.

House gotcha, adopted repo-wide: a class holding `unique_ptr<Forward>`
declares its destructor in the header and defines it `= default` in the .cpp.

---

## 3. Data model (message thread only)

The preset is a `juce::ValueTree`, saved as XML with extension `.fmpreset`.
Only non-empty carts are stored. Identifiers live in `Ids.h`.

```xml
<FlowerPreset schemaVersion="1" name="Morning show">
  <Page name="Jingles">
    <Cart cell="0" title="Sweeper A" path="D:/Lib/sweep_a.wav"  relPath="../lib/sweep_a.wav"  colour="ff3366aa" gainDb="0"  loop="0"/>
    <Cart cell="9" title="Bed news"  path="D:/Lib/bed_news.mp3" relPath="../lib/bed_news.mp3" colour="ff338855" gainDb="-3" loop="1"/>
  </Page>
  <Page name="SFX"/>
</FlowerPreset>
```

- **Cart id** used by the engine: `pageIndex * 64 + cell`. A position, not a
  UID. Removing a page stops its voices. Pages are not reordered in v1.
- **File resolution:** absolute `path` first, then `relPath` relative to the
  preset file. This is what survives the move from the dev PC to the work PC
  when the libraries sit on different drives: keep presets next to, or above,
  the audio folders. A file that resolves nowhere leaves the cart **missing**:
  title and colour kept, drawn as missing, not triggerable, "Relocate…" in its
  menu. The preset always loads.
- `title` defaults to the file name without extension. `colour` is ARGB hex.
  `gainDb` in [−24, +18], default 0. `loop` is 0/1.
- `schemaVersion` is checked on load; `Preset` owns the migration chain.
- **App settings** (`juce::ApplicationProperties`, folder
  `CardamomTools/FlowerMachine`, house convention): audio device state XML,
  renderer choice, window bounds, last preset path, recent presets (8).
  Last preset reopens at launch; unsaved changes prompt on close.

---

## 4. Threads and the audio engine

| Thread | Does | Never does |
|---|---|---|
| Message | UI, model, preset IO, publishes samples, sends commands | touch voice state |
| Audio (device callback) | drains commands, mixes ≤ 64 voices, writes per-cart atomics | allocate, lock, IO, log, destroy a `SampleData` |
| Loader pool | decodes + resamples one file per job | touch engine or model |

House real-time rules apply to `CartEngine` and everything it calls: no heap,
no locks, no IO, bounded loops, `ScopedNoDenormals`. Fixed-size containers
only.

**`SampleData`** — immutable: `AudioBuffer<float>` with 1 or 2 channels at the
device sample rate, length, plus source metadata for the UI (path, rate,
duration). `using SamplePtr = std::shared_ptr<const SampleData>`.

**`SampleSlot`** — one per cart id (`std::array<SampleSlot, MAX_CARTS>`),
copied from `../CardamomTools/Svarog/Source/Generators/Sample/SampleSlot.h`:
a ring of 16 `shared_ptr` slots + atomic live index + message-thread retire
list. `acquire()` on the audio thread is wait-free; the audio thread never
holds the last reference, so no destructor runs there. `publish (nullptr)`
unloads; a voice still playing keeps its sample alive until it ends.

**Voices** — a pool of `MAX_VOICES`, each tagged with a cart id, holding
`SamplePtr`, position, a `DECLICK_MS` linear envelope
(`attack | sustain | release`) and a `juce::SmoothedValue` gain. Mono samples
go to both outputs. Starting a cart also releases every other playing cart
whose `loop` is off (D2 revised): a jingle replaces the jingle before it,
while a looping bed keeps going until its own Stop or STOP ALL. Per cart the
engine keeps two atomics written by the
message thread: `gain` (linear, from `gainDb`) and `loop`. The voice reads
them every block — the gain as the smoother's target, the loop flag when it
reaches the end of the file: wrap to 0 if set, otherwise release. Toggling
loop off while playing lets the cart finish; toggling it on keeps it going.
Gain changes are click-free and immediate.

**Commands** (`juce::AbstractFifo` over `std::array<Command, 256>`, single
producer, single consumer):

| Command | Effect on the audio thread |
|---|---|
| `play (cartId)` | If a voice has this cart id → release it over `DECLICK_MS`; take a free voice, `slot.acquire()`, start from 0 with the attack ramp. Null sample or no free voice → nothing (the controller already prevents both). |
| `stop (cartId)` | Release that cart's voice over `DECLICK_MS`. |
| `stopAll` | Release every voice over `DECLICK_MS`. |
| `flushAll` | Drop every voice at once (device restart). |

No event queue: the UI polls atomics at `UI_REFRESH_HZ`:
`cart[id].playing`, `cart[id].playheadFrames`, `cart[id].lengthFrames`,
`deviceSampleRate`. A voice reaching the end clears `playing` itself.

**Devices** (`AudioEngine`) — `AudioDeviceManager` initialised from the saved
state (`initialise (0, 2, saved, true)`, Kuznya pattern), output only,
stereo. WASAPI shared by default, WASAPI exclusive selectable, DirectSound as
fallback, ASIO off. The work PC's Axia IP-Audio Driver exposes its outputs as
standard Windows (WDM) devices, so WASAPI reaches the Axia mixer; the owner
picks the right "Axia IP-Driver Out n" in Settings. A device or sample-rate
change → `flushAll`, then the visible page reloads (§5). Settings has a
**Test tone** (300 ms, −12 dBFS, 440 Hz, generated) to check routing without
a preset.

---

## 5. Loading — the visible page only

RAM is bounded by one page (owner's decision D5): 64 carts of 10 s stereo at
48 kHz ≈ 240 MB worst case; a 3-minute bed ≈ 69 MB.

- Showing a page (tab click, preset open) requests every non-empty cart of
  that page; each cart goes *loading* → *ready* | *missing* | *error*
  individually. The first carts are ready within tens of milliseconds; a full
  page of 64 MP3s decodes in about a second on four threads. Hidden pages are
  unloaded (`publish (nullptr)`); a cart of a hidden page that is still
  playing keeps playing to the end and is caught by STOP ALL.
- Job (`SampleLoader`, `juce::ThreadPool`, below-normal priority): own
  `AudioFormatManager` → read the whole file → keep the first two channels →
  resample to the device rate with `juce::WindowedSincInterpolator` → build
  `SampleData` → `MessageManager::callAsync` to the controller. A generation
  number per cart discards stale results (file reassigned, page changed).
- **Controller** (message thread): listens to the model (path set → request;
  cart cleared → `publish (nullptr)` + `stop`; `gainDb` / `loop` changed →
  the cart atomics), tracks per-cart status,
  publishes arrived samples, turns clicks into `play`/`stop` commands (a cart
  that is empty, loading or missing is not sent), and reloads the page on
  device changes.
- Formats: WAV, AIFF, FLAC, OGG Vorbis built in; MP3 via JUCE's decoder
  (`JUCE_USE_MP3AUDIOFORMAT=1`); AAC/M4A/WMA via Windows Media Foundation.
  **Not Opus**: `.opus` / `.oga` voice notes from WhatsApp or Telegram are Opus
  and JUCE cannot decode them — convert to WAV/MP3 before use (§10 R4).

---

## 6. UI — functional only; the look comes from the `/design` session

```
┌───────────────────────────────────────────────────────────────┐
│ [File ▾]  Morning show*                    [Settings] [STOP ALL] │
├───────────────────────────────────────────────────────────────┤
│ ▎Jingles ▎ SFX ▎ + ▎                                          │
├───────────────────────────────────────────────────────────────┤
│                    8 × 8 CartButtons                          │
├───────────────────────────────────────────────────────────────┤
│ Axia IP-Driver Out 1 · 48 kHz · loading 12/40                 │
└───────────────────────────────────────────────────────────────┘
```

- **Top bar:** File (New, Open, Save, Save As, Recent), preset name with a
  dirty mark, Settings, STOP ALL.
- **Page strip:** one tab per page, "+" adds a page. Right-click a tab:
  Rename, Fill page from folder… (the folder's audio files, in name order,
  into the page's empty cells; assigned carts untouched), Remove (confirmed;
  stops every voice, §3).
- **CartButton** is a small composite: a play area (title, duration or
  remaining time, progress, colour) plus three child controls — **Stop**,
  **Loop** (latching toggle) and **Gain** (knob or fader, −24…+18 dB, 0 dB
  default, double-click resets). Child controls take their own clicks; only
  the play area triggers.
- **States:** *empty*, *loading*, *ready*, *playing*, *missing / error*
  (tooltip says why). The controls are hidden on empty and missing carts.
- **Mouse:** left click on the play area = play, or restart if playing.
  Stop stops that cart. Right click = menu: Assign file…, Rename…, Colour…,
  Relocate… (when missing), Clear. Drag an audio file from Explorer onto a
  cell = assign (replacing an occupied cell asks first). Double-click an
  empty cell = Assign file….
- **Keyboard:** Esc = STOP ALL; Ctrl+S / Ctrl+O / Ctrl+N. Nothing else.
- **Status line:** device and rate, loading progress, warnings (missing
  files, device lost).
- **Settings:** `AudioDeviceSelectorComponent` (output only), renderer
  (§7), test tone, About (version, JUCE version, OS name and build).

All cart drawing lives in `CartButton` and its three child controls; every
other widget is styled by `FlowerLookAndFeel`. The design session delivers
the visuals — vector rules, or image assets such as a knob film-strip as in
Sophrosyne's `KnobComponent` — and implementing them touches only `UI/`.
Window freely resizable; the grid fills its area with rectangular cells (the
owner found square cells too small at M0; final proportions belong to the
design pass). No edit lock: with restart-on-click there is no gesture that
can accidentally modify a cart.

---

## 7. The Windows 10 work PC

Checked against JUCE 8.0.12 sources and the local toolchain:

- JUCE 8 supports **Windows 10, any release** (the only build-number gate is
  dark-mode detection, ≥ 1809, which fails silently and is irrelevant here).
  If the PC turns out to run Windows 7/8.1, JUCE 8 is out and the plan is
  reassessed. If it is 32-bit, a Win32 configuration is added to the .jucer.
- **Static MSVC runtime (/MT)** in both configurations: one self-contained
  `FlowerMachine.exe`, no VC++ Redistributable to install. Deliberately
  different from the Projucer default used by Kuznya/Sophrosyne.
- **Renderer:** JUCE 8 opens Windows windows with Direct2D; GDI/software is
  available. An old GPU driver is the likeliest failure on an old PC, so
  Settings has *Renderer: Direct2D | Software* (persisted, applied to the
  main window's peer) and the exe accepts `--software-renderer` for the case
  where it does not paint at all.
- **Audio:** WASAPI over the Axia WDM devices (§4). No ASIO SDK needed.
- **No terminal on the work PC:** About shows OS name and build number, so
  the exact Windows release is read from inside the app on first run.
- **Deployment:** `FlowerMachineSetup.exe` (§7.1). The repo lives on the
  owner's git remote (D11), which is also how the exe reaches the work PC;
  build output stays out of the repo.
- Verification on the work PC happens whenever access is available; it does
  not block development. Checklist: window paints (both renderers), Axia
  outputs listed, test tone on the right output, a WAV and an MP3 cart play,
  About shows the build number.

### 7.1 The installer (`Installer/`, added 2026-09-04)

One self-contained `FlowerMachineSetup.exe` that carries the built
`FlowerMachine.exe` inside it, wearing a 1990s cracktro while it works.

- **Per-user, never elevated.** Everything lands in
  `%LOCALAPPDATA%\Programs\FlowerMachine` and `HKEY_CURRENT_USER`, so it
  installs on a station PC where the operator has no administrator rights.
  Local, not roaming: a 7 MB image in roaming AppData would be dragged
  through profile sync. Elevating would also repoint `%LOCALAPPDATA%` at the
  administrator's profile, so the manifest stays `asInvoker`.
- **The payload rides as a Win32 RCDATA resource**, injected through JUCE's
  `JUCE_USER_DEFINED_RC_FILE` hook, not as Projucer `BinaryData`: 7 MB of C
  array is a ~26 MB translation unit rebuilt on every change, while the
  resource compiler simply reads the file. `Installer/pack-payload.py` puts a
  magic/length/checksum header in front of it, because `SizeofResource`
  reports the padded resource size rather than the payload size.
- **It installs:** the program, its icon, a copy of itself as
  `Uninstall FlowerMachine.exe`, Start Menu and desktop shortcuts (written
  with `IShellLinkW` — JUCE's `File::createShortcut` sets no icon and no
  working directory), the Add/Remove Programs entry and the `.fmpreset`
  association. **Nothing under Documents:** a fresh install opens an empty
  cart wall, and the app makes its own presets folder the first time one is
  saved.
- **Uninstall** is the same executable behind `--uninstall`. Windows refuses
  to delete a running image but will rename one, so it moves itself to the
  temp folder, deletes the tree, and leaves a detached batch file to sweep up
  its own image. `MOVEFILE_DELAY_UNTIL_REBOOT` needs an elevated token and is
  therefore out. Presets and settings are kept unless the operator ticks the
  box, and only `CardamomTools\FlowerMachine` is ever removed — the folder
  above it is shared with Kuznya.
- **Music:** "Tarantella Cardamom", written for the intro, four channels
  synthesised in code from a note table (`Source/Tune.h`). No audio file, so
  nothing to license in a public repository. The audio callback follows the
  same real-time rules as `CartEngine`; the device is opened from the first
  timer tick, not the constructor, because scanning driver types blocks the
  message thread for seconds on a PC with Axia WDM endpoints.
- **Known risk:** an execution policy (AppLocker, WDAC, SRP) that only
  permits `%WINDIR%` and `%PROGRAMFILES%` would let the install succeed and
  then block the program from running. Worth testing early on the work PC.

---

## 8. Milestones (each independently runnable)

| # | Deliverable | Verified by |
|---|---|---|
| **M0 — skeleton** | `.jucer` (FlowerMachine, static runtime), `Main`, window with a dummy 8×8 grid and STOP ALL, `AudioEngine` with device init, Settings (device selector, renderer, test tone, About). Release exe. | Claude: MSBuild Debug + Release. Owner: runs it here; on the work PC when possible (§7 checklist). |
| **M1 — sound** | `SampleData`, `SampleSlot`, `CartEngine`, `SampleLoader`, `Controller`; page 1 filled from a folder picked in the File menu (later kept as the page action "Fill page from folder…"); click plays, click restarts, Stop button, Loop, Gain, STOP ALL, remaining time. | Owner: no clicks on start/restart/stop, several carts at once, a long MP3. |
| **M2 — presets** | Model, pages (add/rename/remove), save/load/recent/reopen-last, drag & drop, cart menu, missing files and Relocate, page-only loading. This is a usable v1. | Owner: build a real preset; move preset + library elsewhere and reopen; delete a file and reopen. |
| **M3 — look and ship** | Implement the `/design` visuals in `UI/`; git remote; `CLAUDE.md` and this document updated. | Owner: a real show on the work PC. |

Milestone boundaries are pauses for the owner's verification (house rule).

---

## 9. Decisions

### Resolved with the owner, 2026-09-03

| ID | Decision | Consequence |
|---|---|---|
| D1 | Click on a playing cart **restarts** it. | One trigger mode; no per-cart modes. |
| D2 | *Revised after M1 (2026-09-03):* starting a cart **stops every playing cart that is not looping**; looping carts stop only through their own Stop or STOP ALL. | One rule in the engine's play command, no per-cart flags. The original answer ("not needed") was overturned on hearing jingles overlap. |
| D3 | **No fades.** | Only the 5 ms declick ramp remains. STOP ALL is immediate. |
| D4 | Hotkeys not needed; mouse only. | Dropped. Esc = STOP ALL kept because it is free. |
| D5 | Avoid filling RAM (work PC memory unknown). | **Visible page only** resident (§5). |
| D6 | Axia interface with digital mixer. | WASAPI over the Axia WDM devices; ASIO off (§4). |
| D7 | MP3 and WAV, occasionally OGG. | Covered. Opus voice notes are not (§5). |
| D8 | No touchscreen. | Mouse-only interaction. |
| D9 | Name: **FlowerMachine**. | Namespace `flowermachine`, `.fmpreset`, settings folder `CardamomTools/FlowerMachine`. |
| D10 | UI in English. | — |
| D11 | Docs in the repo; a git remote will follow. | `docs/`; `.gitignore` mirrors Kuznya's. |
| D12 | "Which edit mode?" | The edit lock was a guard against accidental edits on air. Removed: nothing a click can do modifies a cart. |
| O1 | A cart is: title (assigned or file name), a **Stop button**, a **Loop** toggle, a **Gain** control up to +18 dB. | Per-cart `loop` and `gainDb` return to v1 (§4, §6); stop is a button on the cart, not a menu entry. |
| — | Graphics via a dedicated `/design` session: a **new canvas**, opened when M3 starts. | UI described functionally; drawing confined to `UI/` (§6). |
| — | *After M2:* the M1 folder loader was useful — kept as **Fill page from folder…** on the page menu. | Empty cells only, name order (§6). |
| — | *M3 (2026-09-03):* design canvas "FlowerMachine Look", direction A **Studio panel** chosen by the owner. | Values in `Source/UI/Palette.h`; drawing in `FlowerLookAndFeel`, `CartButton`, `MainComponent`. Settings button without the gear icon. |
| — | *After M3:* the owner asked for an **installer** styled like a 90s warez / demoscene installer, and for a **complete** install rather than just the executable. | Amends §7 ("copy the exe, no installer"). The owner supplies the music. |
| — | *2026-09-04:* typeface stays **Segoe UI**, after comparing it with Barlow at the real sizes. | Nothing to embed or license; `Source/UI/Palette.h` is the single place that names it. |
| — | *2026-09-04:* the git repository at `github.com/CardamomFlower/Flower` is the owner's; **Claude does not commit or push.** | Claude prepares the tree; the owner stages and commits. No `Co-Authored-By` trailers anywhere. |

### Open

None. Assumed unless the owner objects: the cart shows remaining time and a
progress bar while playing; carts of a hidden page keep playing when the tab
changes.

### v2 candidates (recorded, not planned)

Exclusive/solo · fades · hotkeys · page reorder · "Collect files next to
the preset" · MIDI trigger · Opus decoding.

---

## 10. Risks

| ID | Risk | Mitigation |
|---|---|---|
| R1 | Direct2D misbehaves on the old GPU. | Software renderer in Settings + `--software-renderer`. |
| R2 | Library paths differ between PCs. | `relPath`, explicit *missing* state, Relocate. |
| R3 | A page switch is followed by an immediate click before decoding finishes. | Carts become ready individually within tens of ms; a loading cart is visibly not ready and ignores the click. |
| R4 | Opus voice notes in the library. | Not supported; convert beforehand. |
| R5 | The work PC is not Windows 10, or is 32-bit. | Reassess / add Win32 configuration (§7). |
