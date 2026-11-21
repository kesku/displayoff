# Display Off

Run `displayoff.exe`. Double-click the tray icon or press **Ctrl+Alt+F12** to turn off the displays. Move the mouse or press a key to wake them.

- For a shortcut that turns off the displays directly, append `--off` to its target.
- To launch at sign-in, put a shortcut to the executable in `shell:startup`.

## Build

With 64-bit MinGW-w64 on your PATH, run `.\build.ps1` in PowerShell. The executable is at `build\displayoff.exe`.

Based on [itsnateai/displayoff](https://github.com/itsnateai/displayoff). [MIT License](LICENSE).
