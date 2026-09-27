# Audio Slicer

An Unreal Engine editor plugin for cutting one Sound Wave into many. Useful when a voice actor
delivers all lines of a scene in one file, or when a sound designer hands over a single take with
twenty footsteps in it.

Mark the parts on the waveform (or let silence detection find them), name them, export. Every
slice becomes a regular Sound Wave asset next to the source or in a folder of your choice.

## Features

- Waveform view with zoom, scrolling and a time ruler, one lane per channel
- Create slices by dragging, resize them by their edges, move them around
- Detect Slices splits a sound at its silent parts, with adjustable threshold and timings
- Slice list with editable names and exact start/end times
- Cuts snap to zero crossings and get short fades, so they don't click
- Sound class, submix, attenuation, concurrency and compression are taken over from the source
- Existing assets are either updated in place (references stay intact) or left alone
- Full undo/redo for every edit
- Usable from Editor Utility Blueprints and Python as well

## Requirements

Unreal Engine 5.4 or newer. The plugin is editor-only and adds nothing to packaged games.

## Installation

From Fab: install it to your engine from the Epic Games Launcher and enable **Audio Slicer** under
Edit > Plugins.

Manually: copy this folder to `YourProject/Plugins/AudioSlicer` and build the project.

## Getting started

Open the window from **Tools > Audio Slicer**, or right-click a Sound Wave in the Content Browser
and choose **Open in Audio Slicer**.

1. Pick the sound at the top.
2. Drag across the waveform to add a slice, or press **Detect Slices**.
3. Adjust the edges, give slices names if you like. Unnamed slices use the base name and a number,
   for example `VO_Hero_01`, `VO_Hero_02`.
4. Pick the target folder and press **Export**.

## Controls

| Input | Action |
| --- | --- |
| Drag on empty space | New slice |
| Drag a slice edge | Resize |
| Drag inside a slice | Move |
| Click | Set the playhead, select the slice under it |
| Double-click a slice | Play it |
| Right-click | Play, split, zoom, delete |
| Mouse wheel | Zoom around the cursor |
| Shift + wheel, middle mouse drag | Pan |
| Space | Play / pause |
| Delete | Delete the selected slice |
| F | Zoom to the selected slice, or to the whole sound |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Esc | Cancel the current drag |

## Options

The Options button in the window, or Editor Preferences > Plugins > Audio Slicer:

- **Name Pattern**: `{Base}` and `{Index}` are replaced, default `{Base}_{Index}`
- **Index Digits**: zero padding for the number
- **Default Subfolder**: where slices go relative to the source, default `Slices`
- **Fade In / Fade Out**: in milliseconds, 0 turns them off
- **Snap To Zero Crossing**, **Copy Source Settings**, **Save After Export**
- **Detect Slices**: threshold, minimum silence, minimum slice length and padding

## Blueprint and Python

`UAudioSlicerLibrary` exposes the same functionality:

```python
import unreal

sound = unreal.load_asset("/Game/Audio/VO_Scene01")
slices = unreal.AudioSlicerLibrary.detect_slices(sound, unreal.AudioSilenceDetectionSettings())

options = unreal.AudioSliceExportOptions()
options.destination_path = "/Game/Audio/VO_Scene01_Slices"
unreal.AudioSlicerLibrary.export_slices(sound, slices, options)
```

## Good to know

- Only imported Sound Waves can be sliced. Procedural sounds and Sound Cues can't.
- Unreal keeps imported audio as 16-bit PCM internally, so that's what the slices contain,
  whatever format the original file was.
- Slices don't remember the source file, so reimporting a slice asks for a file. Reimporting the
  source updates the waveform in an open Audio Slicer window.

## Support

Bugs and feature requests: [GitHub issues](https://github.com/ITemre/AudioSlicer/issues)

## License

The source in this repository is released under the
[PolyForm Noncommercial License 1.0.0](LICENSE). You can use, change and share it for anything
that isn't commercial: personal projects, learning, game jams, research, hobby work.

For commercial projects, get Audio Slicer on Fab. Copies bought or downloaded there are covered by
the Fab license instead of the one in this repository.
