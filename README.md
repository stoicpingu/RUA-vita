<h1 align="center">Robot Unicorn Attack · PS Vita</h1>
<p align="center">Chase your dreams. Jump, dash, and try again.</p>
<p align="center">
  <a href="#installation">Installation</a> ·
  <a href="#controls">Controls</a> ·
  <a href="#building">Building</a>
</p>

A native PS Vita port of the Android 1.03 game, with the original artwork,
music, and gameplay rules. Fills the Vita screen and supports buttons and touch.

## Installation

1. Copy `robot-unicorn-native.vpk` to your homebrew-enabled Vita.
2. Install it with **VitaShell**.
3. Launch **Robot Unicorn Attack** from LiveArea.

Everything is included in the VPK. No extra plugins or data downloads are needed.

## Controls

| Button | Action |
|:---|:---|
| Cross / L | Jump; press again to double jump |
| Hold / release jump | Control jump height |
| Square / Circle / R | Rainbow dash |
| Start | Pause |
| D-pad / left stick | Navigate menus |
| Cross / Circle | Confirm / back |
| Select | Settings on title or results |
| Triangle | Scores on title or results |

Touch the left half of the screen to jump and the right half to dash.
Settings and scores are saved in `ux0:data/robotunicorn-native/`.

## Building

Requires VitaSDK with vita2d, mpg123 and zlib, CMake, and Python 3.9+ with Pillow.
Extract your Android 1.03 APK locally, then run:

```sh
python3 vita-native/tools/import_original.py --original /path/to/extracted-apk
VITASDK=/path/to/vitasdk sh vita-native/tools/build.sh
```

Output: `vita-native/build/robot-unicorn-native.vpk`. Original game assets are
not included in this source repository. Build files and caches stay inside it.

## Credits

Port by **stoicpingu**, developed with AI assistance and tested by the author on
PS Vita. Thanks to the VitaSDK, vita2d, mpg123, and zlib contributors.
Robot Unicorn Attack and its original artwork, music, and other assets belong
to their respective owners. This is an unofficial fan port.

Found a problem? [Open an issue](https://github.com/stoicpingu/RUA-vita/issues).
