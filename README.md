# Camerist

**Turn a USB camera with a gimbal into a PTZ camera, right inside OBS Studio.**

Camerist is an OBS plugin made for the DJI Osmo Pocket 3 in webcam mode, and it works with any USB camera that exposes pan, tilt or zoom controls to Windows. It adds a **Camerist** dock to OBS where you can aim the camera, zoom, save positions and recall them with a click, a hotkey, a Stream Deck button or a scene change.

[Version française](README.fr.md)

## Features

- Live preview of the camera source, with framing grids (thirds, 4×4, center cross, diagonals, safe areas)
- Aim pad (click to aim, drag to follow) or classic sliders for pan, tilt, roll and zoom
- Saved positions with smooth or instant moves, search, reordering
- One OBS hotkey per position, plus reset, turn 180°, zoom in and zoom out (also listed in OBS Settings, Hotkeys)
- Positions linked to scenes: when OBS switches scene, the camera moves to its position
- Stream Deck support through simple web addresses
- All the camera source settings in one place: resolution, FPS, video format, YUV color space and range, buffering, flip, rotation, hardware decoding
- Light, dark or automatic theme, six accent colors
- English and French, following the OBS language

The video stays in OBS: Camerist only sends movement commands to the camera, the same way as the sliders in the source's "Configure Video" window.

## Installation

1. Download the latest Windows installer from the [Releases](../../releases) page and run it.
2. Plug the camera in over USB, switch it to webcam mode and add it to OBS as a **Video Capture Device** source.
3. Start OBS. Open the **Docks** menu and tick **Camerist** if the dock is not visible yet.

Requirements: OBS Studio 31 or newer on Windows 10 or 11.

## Compatible cameras

Camerist works with any USB camera whose Pan, Tilt or Zoom sliders are active in OBS: source properties, "Configure Video", "Camera Control" tab.

| Camera | Status |
| --- | --- |
| DJI Osmo Pocket 3 | Tested: pan, tilt, roll, zoom |
| DJI Osmo Pocket 4 / 4P | Should work (same USB controls according to other projects), to be confirmed |
| USB PTZ cameras (OBSBOT Tiny, Insta360 Link, Logitech PTZ Pro…) | Should work if they expose these controls to Windows, to be confirmed |
| Webcams with zoom only | Only zoom is available |
| Fixed webcams | No movement. Preview and video settings still work |

Tested another camera? Please open an issue so it can be added to this list.

## Stream Deck

Add a **System > Website** action, tick **Access in background** and paste one of these addresses:

| Action | Address |
| --- | --- |
| Go to a position | copy button next to the position in the dock |
| Reset camera | `http://127.0.0.1:8765/api/home` |
| Turn 180° | `http://127.0.0.1:8765/api/flip` |
| Zoom in / out | `http://127.0.0.1:8765/api/zoom/in` and `/api/zoom/out` |
| Small step on an axis | `http://127.0.0.1:8765/api/nudge?axis=pan&delta=5` |

Axes are `pan`, `tilt`, `roll` and `zoom`. Add `?d=0` to a position address to jump there without travel, or `?d=3` for a 3 second move. Every action is also an OBS hotkey, so tools that trigger OBS hotkeys work too.

The local server only listens on `127.0.0.1`, it cannot be reached from another computer.

## Translations

The dock follows the OBS language. Texts live in two places:

- `data/locale/*.ini` for the names shown in OBS (dock title, hotkeys)
- the `I18N` block at the top of the script in `data/web/index.html` for the dock itself

To add a language, copy the `en` entries, translate them and open a pull request.

## Building

Every push to GitHub builds the plugin automatically (GitHub Actions, see `.github/workflows`). Tagging a version such as `0.1.0` creates a release with the installers.

To build locally, follow the instructions of the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate), which this project is based on.

## License

GPL-2.0-or-later, like OBS Studio. Includes [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT).
