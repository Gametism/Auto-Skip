Auto-Skip automatically skips startup videos, splash screens, warnings, and "Press Any Button" prompts by sending configurable keyboard inputs during the first few seconds after the game launches.

Features
Automatically skips startup logos and intro videos.
Can bypass many warning screens and "Press Any Button" prompts.
Configurable keyboard inputs through an INI file.
Lightweight standalone ASI plugin.
No game files are modified.
Automatically stops after a configurable amount of time to avoid interfering with the main menu.
How It Works

When the game starts, Auto-Skip runs in the background and repeatedly sends a burst of configurable key presses (such as Space, Enter, and Escape) for a short, configurable period. Once the configured duration has elapsed, the plugin automatically stops sending inputs and becomes inactive for the remainder of the session.

Installation
Install an ASI Loader if the game doesn't already use one.
Copy Auto-Skip.asi and Auto-Skip.ini into the game's executable folder.
Launch the game.
Configuration

All settings can be adjusted in Auto-Skip.ini, including:

Startup delay
Active duration
Input interval
Key hold time
Keyboard keys to simulate
Foreground-only mode

Note: Auto-Skip relies on simulated keyboard input. Compatibility depends on how each game handles its startup sequence, so results may vary between titles.
