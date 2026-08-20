# Auto-Skip

**Auto-Skip** is a lightweight ASI plugin that automatically advances skippable startup videos, splash screens, legal notices, epilepsy warnings, and "Press Any Button" screens by sending configurable input during game startup.

Version **0.3** is focused on one goal: **skip skippable startup screens as quickly as practical while keeping the input loop bounded and conservative enough for general game compatibility.**

## Features

- Very fast initial input burst designed to catch the first frame a splash screen becomes skippable.
- Slower fallback phase for screens that only begin accepting input later.
- Configurable keyboard input.
- Mouse input support: Left, Right, Middle, Mouse 4, and Mouse 5.
- Keyboard and mouse press/release events are batched into a single `SendInput` call when `KeyHoldMs=0`.
- Minimum interval clamps prevent accidental zero-delay input flooding.
- Only sends input while the game window is active and in the foreground.
- Automatically pauses if the game loses focus and resumes after the game window is stable again.
- Ignores small launcher/configuration windows using configurable minimum dimensions.
- Prevents multiple Auto-Skip instances from running simultaneously for the same executable.
- Automatically stops after a configurable startup window.
- Logs partial or failed `SendInput` calls for troubleshooting.
- Does not patch game memory, hook game functions, modify game files, or alter the game's input code.

## How It Works

After the game window becomes active, Auto-Skip enters a short high-speed phase. During that phase it repeatedly sends complete press/release pairs for the configured keyboard and mouse inputs.

With the default configuration, the first second uses a **5 ms interval**. Auto-Skip then switches to a gentler **20 ms fallback interval** for the remainder of the configured four-second startup window.

This does not make an unskippable screen skippable. It is designed to make screens that already accept player input advance almost as soon as the game begins accepting that input.

## Installation

1. Install an ASI Loader if the game does not already use one.
2. Copy `AutoSkip.asi` and `AutoSkip.ini` into the game's executable directory.
3. Launch the game.

## Configuration

```ini
[AutoSkip]
TargetExe=

StartDelayMs=0
TotalRuntimeMs=4000

FastBurstDurationMs=1000
FastIntervalMs=5
FallbackIntervalMs=20
KeyHoldMs=0

OnlyWhenGameForeground=1
ForegroundStableMs=25
WaitForForegroundMs=15000

MinWindowWidth=640
MinWindowHeight=360

KeyboardKeys=SPACE,ENTER,ESCAPE
MouseButtons=LEFT

EnterAfterMs=0
EscapeAfterMs=0
MouseAfterMs=0

MaxInputBursts=0
```

### Settings

| Setting | Description |
| --- | --- |
| `TargetExe` | Optional executable-name restriction. Leave empty to use the process in which Auto-Skip is loaded. |
| `StartDelayMs` | Optional delay before Auto-Skip begins looking for the game window. |
| `TotalRuntimeMs` | Total active startup period. |
| `FastBurstDurationMs` | Length of the initial high-speed phase. |
| `FastIntervalMs` | Interval between bursts during the fast phase. Values below 5 ms are clamped to 5 ms. |
| `FallbackIntervalMs` | Interval after the fast phase. Values below 10 ms are clamped to 10 ms. |
| `KeyHoldMs` | Optional keyboard hold duration. `0` enables the fastest batched press/release path. |
| `OnlyWhenGameForeground` | Only sends input while a valid game window is in the foreground. |
| `ForegroundStableMs` | How long the game window must remain stable before input begins/resumes. |
| `WaitForForegroundMs` | Maximum time to wait for a usable game window. |
| `MinWindowWidth` | Minimum accepted game-window width. |
| `MinWindowHeight` | Minimum accepted game-window height. |
| `KeyboardKeys` | Comma-separated keyboard keys. Leave empty to disable keyboard input. |
| `MouseButtons` | Comma-separated mouse buttons. Leave empty to disable mouse input. |
| `EnterAfterMs` | Delay before Enter becomes eligible. |
| `EscapeAfterMs` | Delay before Escape becomes eligible. |
| `MouseAfterMs` | Delay before configured mouse clicks become eligible. |
| `MaxInputBursts` | Optional hard cap on successful bursts. `0` means the runtime limit is used instead. |

### Supported Keyboard Keys

- `SPACE`
- `ENTER` / `RETURN`
- `ESC` / `ESCAPE`
- `TAB`
- `BACKSPACE`
- `SHIFT`
- `CTRL` / `CONTROL`
- `ALT`
- `UP`, `DOWN`, `LEFT`, `RIGHT`
- `A-Z`
- `0-9`
- `F1-F24`

### Supported Mouse Buttons

- `LEFT` / `LMB`
- `RIGHT` / `RMB`
- `MIDDLE` / `MMB`
- `X1` / `MOUSE4`
- `X2` / `MOUSE5`

## Compatibility and Stability

Auto-Skip uses the Windows `SendInput` API only. It does not write to game memory or interfere with game code.

The default 5 ms fast interval is intentionally aggressive but bounded. Auto-Skip will not accept a fast interval below 5 ms, and every normal burst contains complete down/up pairs so keys and mouse buttons are not intentionally left held.

Some games use input systems that do not accept synthetic Windows input. Some startup sequences are also intentionally unskippable. Auto-Skip cannot bypass those restrictions.

## Backward Compatibility

Existing v0.2 INI files remain usable. If `PressIntervalMs` is present and the new interval settings are absent, Auto-Skip uses the old value for the v0.3 timing phases, subject to the new safety clamps.

## License

MIT License
