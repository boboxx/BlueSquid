# Device editor and configuration layout — touchscreen 1.0.12

Device Configuration places matching Scan and Save buttons side by side, with
its status/description below. The light-group description is inside its card;
card labels and description use the light text colour.

Edit device includes a Colour dropdown with Default, Cyan, Green, Yellow,
Amber, Red, Blue, Purple and White. The icon preview follows the selection.
Save stores a per-device palette choice in touchscreen preferences. Dashboard
icons use the selected accent (with contrasting glyphs when active), and device
configuration/list icons use that colour. Default retains each control's normal
accent. Leaving without Save does not commit the colour.

SD export/import includes optional device_icon_colours. Existing exports without
this field retain current colours; out-of-range values reject the import.
Controller firmware remains 1.0.12; only touchscreen_controller requires upload.

## Touchscreen 1.0.13

The named colour dropdown is replaced by a colour-preview button opening a
3-by-3 swatch picker. Eight colour blocks and a separate Default/reset tile
retain the same saved palette IDs. The selected tile is outlined; tapping a
swatch updates the preview, and Edit device Save commits it as before.

## Touchscreen 1.0.14

Icon and colour now share one picker with a large live preview, two rows of six
circular swatches based on the supplied reference, and the ten available icons
below. The selected swatch has an outer ring; the selected icon uses its colour
on a highlighted tile. Done returns to Edit device, where Save commits both.
The separate colour page and icon button-matrix code have been removed.
New palette entries are appended so older saved colours and exports retain their
original values. Default/reset remains available. Controller stays 1.0.12.

## Touchscreen 1.0.15

The preview sits left of the colour circles. Done and Default are removed;
a checkmark saves the selected icon and colour to preferences immediately and
returns to Edit device. Name, visibility and order still use the editor's Save.

## Touchscreen 1.0.16

All five explicit Save/confirm buttons use a checkmark, sized and typeset from
the Edit device keyboard's confirm key layout and item font. Configuration,
Wi-Fi, hotspot, device editor and icon picker actions retain their behavior.
Edit device removes Show; Order offers Hidden followed by positions 1–8.
Hidden preserves the stored order, while choosing a number makes the device
visible and swaps positions using the existing ordering behavior. Visibility
and order remain stored under the existing preference keys.
