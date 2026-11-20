# T-Minus

T-Minus is an OBS Studio plugin for running countdowns, stopwatches and "time until" timers from a dock, showing them on stream through text sources, and running actions in OBS when they finish.

## Features

### Timer types

| Type | What it does | Typical use |
|------|--------------|-------------|
| **Countdown** | Counts down a fixed length (days, hours, minutes, seconds), with quick presets from 1 minute to 1 hour. | "Starting soon", BRB screens |
| **Stopwatch** | Counts up from zero. | Stream uptime, segment timing |
| **Time Until** | Counts down to a chosen date and time, set with a built-in date and time picker. | Event start times |

Countdowns and stopwatches are measured on a monotonic clock, so changes to the system clock don't make them jump. Time Until timers follow the wall clock.

Countdown and Time Until timers can optionally run into **overtime**: instead of stopping at zero they keep counting, shown with a prefix of your choice (`-0:15` by default). Their end steps still run once, at zero.

### The T-Minus dock

- Add as many timers as you need, and reorder them by dragging.
- Each timer has Start, Pause, Resume and Reset controls, plus up to three quick add-time buttons.
- Each timer's menu can edit, duplicate, move or delete it.
- The toolbar has **Start All** and **Pause All** buttons and shows how many timers are running.
- **Compact View** shrinks each timer to a single row.
- Timers belong to the current **scene collection**. Switching collections loads that collection's timers. Renaming a collection keeps its timers, and deleting one removes them.

### Showing a timer on stream

- Link each timer to a **text source** (Text (GDI+) on Windows, Text (FreeType 2) on macOS and Linux) and T-Minus keeps the source up to date.
- Wrap the time in your own text with placeholders, for example `Starting in %time%` or `Back at %target%`:
  - `%time%`: the time
  - `%name%`: the timer's name
  - `%target%`: the clock time the timer reaches zero
  - `%percent%`: how far through a countdown it is (0 to 100)
- Pick a time format:
  - Auto, which hides hours and days while they're zero
  - `H:MM:SS`, `MM:SS` or `D:HH:MM:SS`
  - Seconds only
  - Custom, where you choose which units to show
- Hidden larger units carry into the first unit shown, so 1 hour 5 minutes in `MM:SS` reads `65:00`.
- Optionally pad the first unit (`05:00` rather than `5:00`).
- Optionally show tenths or hundredths of a second (`0:05.3`, `0:05.27`).
- Optionally replace the time with your own text once the timer ends, such as "We're live!".

### The T-Minus Timer source

Add a **T-Minus Timer** source to a scene instead of linking a text source. It shows the timer's text (with its format string and finished text) and draws it through its own shader, so it looks the same on every platform:

- **Text**: font, outline, alignment, and a minimum width so the source doesn't resize as the digits change.
- **Fill**: solid, or a two- or three-color gradient, linear (at any angle) or radial, optionally animated.
- **Warning** and **Final seconds**: their own colors or gradients from a number of seconds left, with an optional smooth fade between states and a pulse in the final seconds.
- **Progress** (countdowns): a bar below, above or behind the text, or a ring around it, with rounded ends, a track color, and its own gradient or the text's colors.
- **Background**: a panel behind everything, with its own color or gradient, padding and corner radius.

### Actions

On the timer's **Actions** page you can run a sequence of steps when a countdown or Time Until timer reaches zero, and add **time triggers** that run their own steps while the timer runs: when the time left passes a point (for example 0:30 left), or for stopwatches when the time elapsed does (for example 1:00:00 of uptime).

| Step | What it does |
|------|--------------|
| **Switch Scene** | Changes the program scene. |
| **Show/Hide Source** | Shows or hides a source in a scene. |
| **Enable/Disable Filter** | Turns a filter on a source on or off. |
| **Set Text** | Writes text to the timer's text source (supports the placeholders above). |
| **Control Media** | Plays, pauses, restarts or stops a media source. |
| **Mute or Set Volume** | Mutes, unmutes or toggles an audio source, or sets its volume in dB. |
| **Stream, Record or Replay** | Starts or stops streaming, recording or the replay buffer, pauses or resumes recording, or saves a replay. |
| **Control Timer** | Starts, pauses, resets or adds time to another timer, so timers can be chained. |
| **Press OBS Hotkey** | Triggers any OBS hotkey. |
| **Press Source Hotkey** | Triggers a hotkey that belongs to a source. |
| **Wait** | Pauses the sequence for a set time. |

Steps run top to bottom. **Test Run** lets you try a sequence right away, and resetting or restarting the timer cancels sequences that are still running.

### When streaming starts

Each timer can do one of the following when you start streaming:

- Do nothing
- Start (or resume)
- Restart from the beginning
- Reset and wait

### Hotkeys

You bind all T-Minus hotkeys in **OBS Settings › Hotkeys**:

- **T-Minus: Start All Timers** and **T-Minus: Pause All Timers**
- Per timer: **Start/Pause** and **Restart**, listed as `T-Minus: … (timer name)`
- Per timer: **add-time amounts**. Each amount you define (positive to add time, negative to subtract) gets its own hotkey, and the first three also appear as buttons on the timer in the dock.

### Remote control (obs-websocket)

With obs-websocket (built into OBS), T-Minus registers the vendor `t-minus`. Send requests with `CallVendorRequest` (`vendorName: "t-minus"`), naming a timer by ID or name in `timer`:

| Request | Data | Notes |
|---------|------|-------|
| `GetTimers` | | Returns `timers`: each timer's `id`, `name`, `type`, `state`, `timeMs` and `time`. |
| `StartTimer`, `PauseTimer`, `ResetTimer` | `timer` (optional) | Without `timer`, applies to every timer. |
| `ToggleTimer` | `timer` | Starts or pauses. |
| `AddTime` | `timer`, `seconds` | Negative values subtract time. |
| `SetTime` | `timer`, `seconds` | Sets the time left (or elapsed, for stopwatches). |

Each response has `success`, plus `error` when it fails. T-Minus also emits the vendor events `TimerStateChanged` and `TimerFinished` with the same fields as `GetTimers`, so tools such as Stream Deck and Touch Portal can follow along.

## Requirements

- **OBS Studio 33 or later.** T-Minus uses the OBS 33 plugin module layout and does not support earlier versions.
- Windows 10/11 (x64), macOS 12 or later (Apple silicon and Intel), or Ubuntu 24.04 (x86_64).

## Installation

Download the package for your platform from the [Releases](https://github.com/Voidscape-Development/T-Minus/releases) page:

| Platform | Package |
|----------|---------|
| Windows | `t-minus-<version>-windows-x64.zip` |
| macOS | `t-minus-<version>-macos-universal.pkg` |
| Ubuntu | `t-minus-<version>-x86_64-linux-gnu.deb`, installed with `sudo apt install ./t-minus-<version>-x86_64-linux-gnu.deb` |

Once it's installed, restart OBS and open the dock from **Docks › T-Minus**.

A rolling **nightly** pre-release is rebuilt from every commit to `master`. It is untested; use a tagged release for production.

## Building from source

T-Minus is built on the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate) and uses its CMake presets and build scripts.

| Platform | Tools |
|----------|-------|
| Windows | Visual Studio 17 2022, CMake 3.30.5+ |
| macOS | Xcode 26, CMake 3.30.5+ |
| Ubuntu 24.04 | CMake 3.28.3+, `ninja-build`, `pkg-config`, `build-essential` |

Configure and build with the preset for your platform (`windows-x64`, `macos` or `ubuntu-x86_64`):

```sh
cmake --preset <preset>
cmake --build --preset <preset>
```

The OBS sources, prebuilt dependencies and Qt 6 that the build needs are listed in `buildspec.json` and downloaded during configuration on Windows and macOS. See the template's [build system documentation](https://github.com/obsproject/obs-plugintemplate/wiki) for details.

### Continuous integration

GitHub Actions builds every push and pull request on Windows, macOS and Ubuntu, and checks formatting with `clang-format` and `gersemi`.

- Pushing a semantic-version tag (e.g. `1.2.0`) creates a draft release with packages attached.
- Every push to `master` updates the nightly pre-release.

## License

T-Minus is licensed under the [GNU General Public License v2.0](LICENSE).
