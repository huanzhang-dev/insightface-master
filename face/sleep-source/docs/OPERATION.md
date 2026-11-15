# Operation

Build with `build_vs2019.cmd` or `build_vs2015.cmd`; generated files stay under `Z:\sleep-build`. Run `run_ir_camera.cmd`, select the camera index/FPS/eyewear, and look forward with eyes open for five usable seconds. Q/Esc quits; R resets calibration for a new subject. The launcher uses a separate output folder for each session.

```bat
Z:\sleep-build\v142-x64\bin\Sleep.exe --source 0 --infrared --fps 30 --sound --output Z:\sleep-data\output\session1
Z:\sleep-build\v142-x64\bin\Sleep.exe --source 0 --infrared --fourcc "Y16 " --raw-ir --ir-black 1000 --ir-white 5000
Z:\sleep-build\v142-x64\bin\Sleep.exe --source 0 --roi 100,40,400,400 --eyewear opaque
```

Use a Windows Media Foundation-compatible NIR camera and suitable illumination. Infrared preprocessing alone cannot make an RGB-only camera infrared. Sensor controls and useful eye detail through eyewear depend on actual hardware and driver support. Camera indices come from Media Foundation enumeration and may differ from DirectShow indices.

`--fps`, `--width`, `--height` request the closest native camera mode; actual frame rate can differ. Normal capture requests decoded RGB32 (MJPEG uses full-range YUY2 and native BGR conversion), then normalizes to BGR. `--raw-ir` selects native Y16 by default; use `--fourcc` for an exposed supported Y8/Y800 subtype. Fixed black/white mapping is applied to unsigned 16-bit data. Unsupported native layouts fail explicitly; no opaque packed layout is silently treated as gray pixels.

Keep one subject in the ROI. Multiple detections become unknown. The subject continuity reset is a bounding-box/time heuristic, not biometric identification; press R if people swap in the same place. Default pose gates: |yaw|<=40, |pitch|<=35, |roll|<=45. Generic pose estimates are approximate. If eyes are blocked, quality checks fail, or the face is too small/poorly oriented, the state is `UNABLE_TO_ASSESS`.

| State | Meaning |
|---|---|
| CALIBRATING | Gathering usable open-eye observations. |
| MONITORING | No current cue in the available evidence; not proof of wakefulness. |
| EYES_CLOSED | Timing measured bilateral eye closure. |
| DROWSINESS_ALERT | A rule triggered; stays latched during poor view. |
| UNABLE_TO_ASSESS | Current eye state cannot be verified. |

Known IR-opaque lenses: `--eyewear opaque`; eye-state claims are disabled. Automatic occlusion checks can miss an obstruction. Three seconds of unusable eyes produce an observation fault. Sound is opt-in (`--sound`), no more often than every five seconds. A camera read error is a fatal diagnostic (exit 1); explicit camera EOF/loss is logged and exits 2. File EOF or Q/Esc exits 0. A hung native driver call cannot be timed out by this implementation.

`--ear-threshold`, `--closure-seconds`, `--perclos`, `--window`, `--min-valid`, `--yaw-limit`, `--pitch-limit` allow tuning. Personal calibration replaces the initial EAR threshold with 65% of median open-eye EAR, bounded to 0.12..0.24. Closed eyes at startup cannot establish an open-eye baseline; the configured closure rule still works before calibration. PERCLOS here is EAR-threshold closure fraction, not a calibrated clinical eyelid-closure measure.

Default output is `Z:\sleep-data\output`; direct CLI runs overwrite existing fixed names. Use a fresh leaf `--output` directory under an existing parent to retain sessions. `monitor.avi` records the preview; after 60 seconds recording rotates to `monitor_1.avi`, etc. `observations.jsonl` contains source frame/time, reason, EAR, pose, visibility, closure, coverage and alerts. `performance.json` records achieved FPS, inference p95, dropped/stale results and capture status. JSON timestamps are authoritative; fixed-rate AVI timing is approximate when preview frames are missed.

Native MJPEG AVI input supports this program's single video stream with JPEG chunks. Other file codecs/containers are delegated to installed Media Foundation decoders. AVI output has no audio. `--offline` processes synchronously as fast as possible and reports throughput, not live camera FPS. `physical_ir_camera_tested:false` is shipped validation status, not automatic classification of the attached sensor.
