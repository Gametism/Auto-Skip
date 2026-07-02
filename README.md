Auto-Skip

Auto-Skip is a lightweight ASI plugin that automatically skips startup videos, splash screens, legal notices, epilepsy warnings, and "Press Any Button" screens by simulating configurable keyboard input during game startup.

Unlike mods that replace or remove video files, Auto-Skip works entirely through automated input, requiring no modifications to the game's assets.

Features
Automatically skips startup logos and intro videos
Bypasses many warning screens and "Press Any Button" prompts
Configurable through a simple INI file
Lightweight standalone ASI plugin
No game files are modified
Automatically stops after a configurable duration
Compatible with any game that supports ASI plugins and responds to keyboard input during startup
How It Works

When the game launches, Auto-Skip becomes active for a configurable period of time. During this window it repeatedly sends a configurable burst of keyboard inputs (such as Space, Enter, and Escape) to simulate user interaction.

After the configured duration has elapsed, Auto-Skip automatically stops sending input and remains inactive for the rest of the game session.

Installation
Install an ASI Loader if your game does not already use one.
Copy Auto-Skip.asi and Auto-Skip.ini into the game's executable directory.
Launch the game.
Configuration

Example:

[AutoSkip]

StartDelayMs=0
TotalRuntimeMs=5000
PressIntervalMs=25
KeyHoldMs=10

OnlyWhenGameForeground=1

KeyboardKeys=SPACE,ENTER,ESCAPE
Available Settings
Setting	Description
StartDelayMs	Delay before Auto-Skip begins sending input.
TotalRuntimeMs	Total active time before Auto-Skip automatically stops.
PressIntervalMs	Time between input bursts.
KeyHoldMs	How long each key is held before release.
OnlyWhenGameForeground	Only send input while the game window is focused.
KeyboardKeys	Comma-separated list of keys to simulate.
Compatibility

Auto-Skip works with games that:

Support ASI plugins.
Accept keyboard input during startup.
Allow startup videos or splash screens to be skipped via keyboard input.

Compatibility varies between games, as some titles use custom input systems or disable input during startup.

MIT License.
