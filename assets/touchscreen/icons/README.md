# BlueSquid touchscreen icons

These PNG files are editable source copies of the custom icons embedded in the
touchscreen firmware.

When editing an icon:

- preserve the transparent background;
- use white for the visible artwork-the UI applies its colour at runtime;
- keep the existing canvas dimensions unless the firmware glyph dimensions
  will also be updated;
- preserve the existing filename.

After editing, the PNG must be converted back to a 4-bit alpha mask in
`src/touchscreen/LightbulbFont.cpp` before building the firmware.

Run `swift tools/export_touchscreen_icons.swift` from the project root only
when you want to overwrite these PNGs with the masks currently embedded in the
firmware.

The spigot uses SF Symbols `spigot.fill`, imported as a 24×24 A4 glyph.
On macOS, run `swift tools/import_spigot_symbol.swift` to regenerate its
embedded bitmap, then run the exporter above to refresh `spigot.png`.

The Flames icon comes from the supplied `flames-source.png` silhouette.
Run `swift tools/import_flames_icon.swift` on macOS to regenerate its 24×24
A4 glyph, then run the exporter to refresh `flames.png`. White areas become
transparent; the UI applies the icon colour at runtime.
