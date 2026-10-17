# Gemini Deco

A Windows Geode mod that adds an **AI Deco** button to the level editor. Enter a Gemini API key and a one-line style prompt; the mod asks Gemini for a layout, checks its response, then asks before adding decoration-only objects to the visible area. Added objects are editable and use the editor's Undo action.

## Build

Install the Geode SDK, Geode CLI, CMake, and a supported Windows C++ toolchain. From this folder, run:

```powershell
geode build
```

The project targets Geometry Dash 2.2081 on Windows and requires Geode 5 or newer.

## Gemini key

Create a key in Google AI Studio, enter it in the popup, and click Generate. The mod stores the key in its local Geode save data so it does not need to be pasted each time; **Forget key** removes it. The key is sent directly to Google's Gemini API over HTTPS. The mod does not include or transmit a bundled key.

Generated output is limited to 64 objects and accepted only when Geometry Dash identifies each object as a decoration. The mod clamps positions, scale, and rotation and never accepts triggers or gameplay objects.

## Build troubleshooting

- Build from a short path with **no spaces or parentheses**, e.g. `C:\dev\gemini-deco` (not OneDrive/Desktop).
- Delete any old `build` folder before building.
- `GEODE_SDK` must be set (`geode sdk path` shows it) and the SDK must match `mod.json`'s `"geode"` version (`geode sdk version`).
- Run `geode sdk install-binaries` once if you haven't, then `geode build`.
