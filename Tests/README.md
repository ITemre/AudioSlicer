# Audio Slicer regression checks

Run `qa_audio.py` using UnrealEditor-Cmd's PythonScript commandlet in a disposable editor project with AudioSlicer, PythonScriptPlugin and EditorScriptingUtilities enabled. The script imports generated mono/stereo WAVs and writes assets under a unique `/Game/AudioSlicerQA_*` folder. Do not run it in a production project.

Example (PowerShell; adjust the engine, project and script paths):

```powershell
& 'C:/Program Files/Epic Games/UE_5.4/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/QA/AudioSlicerQA.uproject' -run=pythonscript '-script=C:/Source/AudioSlicer/Tests/qa_audio.py' -unattended -nop4 -nosplash -NullRHI -stdout
```

Results are written to `Saved/AudioSlicerQA/qa_results.json`. Check for failures as well as the commandlet exit code. Expected rejected inputs produce warnings.

Validated on Windows with UE 5.4.4 (62 checks passed, two NaN input groups skipped because the engine's Python binding discards NaN assignments) and UE 5.8 (66 checks passed). Coverage includes detection, durations, playback settings, saved assets, unique names, overwrite identity, source protection, duplicate names, empty ranges, invalid numbers and large ranges/fades/padding.

These commandlet checks do not exercise mouse gestures, preview playback or packaged-game playback. The small UI range-clamping changes still need a manual interaction check.
