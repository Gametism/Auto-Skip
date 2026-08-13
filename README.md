# Auto-Skip

**Auto-Skip** is a lightweight ASI plugin that automatically skips startup videos, splash screens, legal notices, epilepsy warnings, and "Press Any Button" screens by simulating configurable keyboard input during game startup.

Unlike mods that replace or remove video files, Auto-Skip works entirely through automated input, requiring no modifications to the game's assets.

## Features

- Automatically skips startup logos and intro videos.
- Bypasses many warning screens and "Press Any Button" prompts.
- Supports multiple configurable keyboard keys.
- Sends each key as an individual press to prevent conflicting simultaneous inputs.
- Only sends input while the game window is active and in the foreground.
- Automatically pauses if the game loses focus and resumes once the game window is active again.
- Ignores small launcher or configuration windows based on configurable minimum window dimensions.
- Prevents multiple Auto-Skip instances from running simultaneously for the same game executable.
- Automatically stops after a configurable duration.
- Configurable through a simple INI file.
- Lightweight standalone ASI plugin.
- No game files are modified.
- Compatible with games that support ASI plugins and accept keyboard input during startup.

## How It Works

When the game launches, Auto-Skip waits until a valid game window is active and in the foreground. During the configured active period, it repeatedly simulates the configured keyboard inputs, such as **Space**, **Enter**, and **Escape**.

Each configured key is sent as a separate press and release rather than all keys being held simultaneously. This allows Auto-Skip to remain fast while reducing the chance of unintended input behavior during startup.

If the game loses focus or its active window changes, Auto-Skip temporarily stops sending input until a valid game window is detected again.

After the configured duration has elapsed, Auto-Skip stops sending input and remains inactive for the rest of the game session.

## Installation

1. Install an ASI Loader if your game does not already use one.
2. Copy `Auto-Skip.asi` and `Auto-Skip.ini` into the game's executable directory.
3. Launch the game.

## Configuration

Example:

```ini
[AutoSkip]
TargetExe=

StartDelayMs=0
TotalRuntimeMs=5000

PressIntervalMs=10
KeyHoldMs=0

OnlyWhenGameForeground=1
ForegroundStableMs=50
WaitForForegroundMs=15000

MinWindowWidth=640
MinWindowHeight=360

KeyboardKeys=SPACE,ENTER,ESCAPE
EnterAfterMs=0
EscapeAfterMs=0

MaxKeyPresses=0
```

### Available Settings

| Setting | Description |
| --- | --- |
| `TargetExe` | Optional executable name restriction. Leave empty to allow the plugin to run in the process in which it is loaded. |
| `StartDelayMs` | Delay before Auto-Skip begins processing startup input. |
| `TotalRuntimeMs` | Total active time before Auto-Skip automatically stops. |
| `PressIntervalMs` | Delay between each sequence of configured key presses. |
| `KeyHoldMs` | How long each individual key is held before release. Set to `0` for immediate press/release. |
| `OnlyWhenGameForeground` | Only sends input while a valid game window is in the foreground. |
| `ForegroundStableMs` | How long the game window must remain stable before Auto-Skip begins or resumes sending input. |
| `WaitForForegroundMs` | Maximum time Auto-Skip waits for a valid foreground game window. |
| `MinWindowWidth` | Minimum game-window client width accepted by Auto-Skip. |
| `MinWindowHeight` | Minimum game-window client height accepted by Auto-Skip. |
| `KeyboardKeys` | Comma-separated list of keyboard keys to simulate. |
| `EnterAfterMs` | Delay before Enter becomes eligible for automated input. |
| `EscapeAfterMs` | Delay before Escape becomes eligible for automated input. |
| `MaxKeyPresses` | Maximum number of simulated key presses. Set to `0` for no additional limit. |

### Supported Keys

Auto-Skip currently recognizes:

- `SPACE`
- `ENTER` / `RETURN`
- `ESC` / `ESCAPE`
- `TAB`
- `BACKSPACE`
- `A-Z`
- `0-9`
- `F1-F24`

Multiple keys can be specified using commas:

```ini
KeyboardKeys=SPACE,ENTER,ESCAPE
```

## Compatibility

Auto-Skip works with games that:

- Support ASI plugins.
- Accept simulated keyboard input during startup.
- Allow startup videos, splash screens, warnings, or prompts to be skipped using keyboard input.

Compatibility varies between games. Some titles use custom input systems, ignore simulated keyboard input, or prevent startup sequences from being skipped entirely.

Auto-Skip does not modify or remove the underlying videos, images, or game assets. It can only skip screens that the game itself allows to be advanced through keyboard input.

## License

MIT License
