# Video verification: registration, detection, tracking and authentication

Verified on **2026-11-13**, Asia/Shanghai, on this Windows development PC using
VS2019 Release x64. This report records fresh executions and saved evidence.

## Finding

**Tracking worked in all six MP4 runs.** In 720 decoded frames and 22 person/run
observations, every person kept exactly one track ID, with zero ID changes.
There were 2,350 detections for 2,352 visible ground-truth face instances, no
unmatched false detections, no wrong identities and no verified strangers.
All 14 registered person/run observations authenticated within four seconds of
first detection. These results apply to the generated footage tested here.

A separate 640x480 lifecycle regression failed because its fixed actor coordinates
place people outside that small frame. Its failures and the Full HD repeat are
recorded below; the MP4 results are not a claim that every project test passed.

## Material and configuration

| Material | Composition | Runs |
|---|---|---|
| `data/videos/fhd_3people/video.mp4` | 2 registered people, 1 stranger | Synchronous and real-time worker |
| `data/videos/fhd_5people/video.mp4` | 3 registered people, 2 strangers | Synchronous |
| `data/videos/fhd_cam_5people/video.mp4` | 3 registered people, 2 strangers; simulated room, light, lens, noise and compression | Synchronous |
| `build/video_verification_20261113/masked_3_source/video.mp4` | 2 registered people, 1 stranger; face-only masked portraits | Synchronous and real-time worker |

Each clip is 1920x1080, 15 fps, 120 frames, about eight seconds. The clips are
**generated**, including the simulated camera-look clip; no real camera recording
was available. Original clips and portraits were preserved. The new masked clip
uses `data/portraits_masked`, retaining faces without original portrait backgrounds.
Only masked stills are embedded below.

The profile was `models/permissive.ini`: YuNet detector, SFace extractor, ONNX
Runtime, 128-dimensional features, cosine threshold **0.550**, two inference
threads. The pipeline reports CPU-only operation. Timings are local measurements,
not performance guarantees for another CPU or a different model profile.

The dashboard supports photos and camera snapshots. This verification runs the
same `AuthPipeline` engines through the `check_video` command in `_test.exe`; it
does not demonstrate a video playback control inside the dashboard.

## Verification sequence

1. **Build:** build `_test.sln`, Release|x64, with VS2019 MSBuild. The runner contains
   the video generator, video verifier, tracker unit checks and pipeline regression.
2. **Prepare face-only video:** generate three moving sprites from the masked
   dataset, with staggered arrivals, increasing face size and in-plane head tilt.
   Save `video.mp4`, numbered frames, `scenario.txt` and `truth.csv`. Read back the
   MP4's first frame to check orientation/compression: the mean difference was
   2.32 against the original and 25.98 against the upside-down image, so it passed.
3. **Register before playback:** read `scenario.txt`; enroll only people marked
   `registered` using `AuthPipeline::enroll_image`. This detects a face, obtains
   landmarks, crops/aligns it, extracts its feature, and persists the record.
   Add 20 filler identities not present in the video. Each three-person run searches
   a gallery of 22 people; each five-person run searches 23. Strangers are absent.
4. **Audit registration:** independently read each saved `people.csv`. Confirm
   expected IDs are present, strangers are absent, and every feature contains 128
   finite values with squared norm within 0.00001 of one. All six databases passed.
5. **Decode and detect:** use the Media Foundation video source, passing each frame
   and capture timestamp to `AuthPipeline::process`. Match detections to visible
   truth boxes at IoU **strictly greater than 0.4**. Each detection can serve at most
   one truth face in a frame. Require at least 90% detection per person.
6. **Track:** the tracker predicts boxes using smoothed velocity and associates
   detections greedily by IoU, requiring at least 0.3 overlap. Record each actor's
   track IDs and count transitions. Require exactly one ID and zero changes for
   these uninterrupted walking clips. IDs restart in each run; they are not
   permanent identity numbers across clips.
7. **Gate and extract:** for an unverified track, refine landmarks and check pose
   against pitch/yaw/roll tolerances of 5/5/8 degrees. Require an 80-pixel face width;
   pose rechecks are at most once per 100 ms. A due face crop is stored and its feature
   extracted; unverified crops are refreshed no faster than once per 1,000 ms.
   Worker mode runs pose/cropping/extraction on another thread and applies results
   on later frames. A verified track is normally followed without repeated extraction.
8. **Authenticate:** match available face features with all enrolled features in
   one M:N call. Accept similarity at or above 0.550. Check every verified frame
   directly against the expected identity, not merely against its first reported
   identity. Require correct recognition within four seconds of first detection,
   and require strangers to remain unverified. Track continuity and identity
   correctness are separate checks.
9. **Verify completion:** require decoded frame count to equal truth frame count,
   truth present for every decoded frame, zero failed processing calls, and one
   successful annotated-frame write per frame.
   Save an annotated MP4, per-frame CSV, registration database, stills and text log
   to a separate directory for each run.
10. **Independently audit evidence:** recompute truth associations, track-ID sets,
    ID transitions and wrong verified identities from the saved CSVs, and audit
    gallery contents. All six MP4 runs passed this independent audit.

## Measured MP4 results

Detection counts are detected/visible face instances across all actors. Recognition
latency is measured from first detection to first correct verification, in seconds.
Processing time is median/p95/max milliseconds inside `process()`; it excludes
video decoding, drawing, encoding, playback waits and worker computation that the
call does not await.

| Run | Frames | Detection | ID changes | Recognition latency (s) | Process median / p95 / max (ms) | Result |
|---|---:|---:|---:|---:|---:|---|
| existing_3_sync | 120 | 333/333 | 0 | 1.33-1.60 | 8.2 / 54.1 / 108.9 | PASS |
| existing_3_worker | 120 | 333/333 | 0 | 1.47-1.67 | 6.8 / 14.7 / 19.1 | PASS |
| existing_5_sync | 120 | 510/510 | 0 | 0.67-1.60 | 8.0 / 50.6 / 73.7 | PASS |
| camera_5_sync | 120 | 510/510 | 0 | 0.93-1.47 | 8.6 / 49.2 / 77.9 | PASS |
| masked_3_sync | 120 | 332/333 | 0 | 1.47-2.00 | 8.6 / 63.1 / 102.3 | PASS |
| masked_3_worker | 120 | 332/333 | 0 | 1.60-2.07 | 7.5 / 14.1 / 19.1 | PASS |

Both worker runs played in about 8.0 seconds. Their maximum processing times stayed
below the verifier's 60 ms per-frame worker limit. Synchronous processing sometimes
exceeded the 66.7 ms frame budget; those were offline runs, not proof of real-time
synchronous delivery.

Each masked run missed the initial small face detection of one actor (119/120
frames for that person). Its later track kept the same ID. Detection was therefore
not perfect, even though tracking continuity and identity checks passed.

### Per-person evidence

| Run | Actor label | Scenario | Detected/visible | Observed ID | ID changes | Authentication |
|---|---|---|---:|---:|---:|---|
| existing_3_sync | august_pfluger | registered | 120/120 | 1 | 0 | 1.60 s |
| existing_3_sync | austin_scott | registered | 111/111 | 2 | 0 | 1.33 s |
| existing_3_sync | ayanna_pressley | stranger | 102/102 | 3 | 0 | Rejected throughout |
| existing_3_worker | august_pfluger | registered | 120/120 | 1 | 0 | 1.67 s |
| existing_3_worker | austin_scott | registered | 111/111 | 2 | 0 | 1.47 s |
| existing_3_worker | ayanna_pressley | stranger | 102/102 | 3 | 0 | Rejected throughout |
| existing_5_sync | blake_moore | registered | 120/120 | 1 | 0 | 1.60 s |
| existing_5_sync | chris_jacobs | registered | 111/111 | 2 | 0 | 0.80 s |
| existing_5_sync | cliff_bentz | registered | 102/102 | 3 | 0 | 0.67 s |
| existing_5_sync | cori_bush | stranger | 93/93 | 4 | 0 | Rejected throughout |
| existing_5_sync | deborah_ross | stranger | 84/84 | 5 | 0 | Rejected throughout |
| camera_5_sync | blake_moore | registered | 120/120 | 1 | 0 | 1.47 s |
| camera_5_sync | chris_jacobs | registered | 111/111 | 2 | 0 | 0.93 s |
| camera_5_sync | cliff_bentz | registered | 102/102 | 3 | 0 | 0.93 s |
| camera_5_sync | cori_bush | stranger | 93/93 | 4 | 0 | Rejected throughout |
| camera_5_sync | deborah_ross | stranger | 84/84 | 5 | 0 | Rejected throughout |
| masked_3_sync | adriano_espaillat | registered | 119/120 | 1 | 0 | 2.00 s |
| masked_3_sync | andrew_clyde | registered | 111/111 | 2 | 0 | 1.47 s |
| masked_3_sync | ayanna_pressley | stranger | 102/102 | 3 | 0 | Rejected throughout |
| masked_3_worker | adriano_espaillat | registered | 119/120 | 1 | 0 | 2.07 s |
| masked_3_worker | andrew_clyde | registered | 111/111 | 2 | 0 | 1.60 s |
| masked_3_worker | ayanna_pressley | stranger | 102/102 | 3 | 0 | Rejected throughout |

### Visual evidence from the masked video

Yellow indicates an unverified track; green indicates a verified identity. Overlays
show track ID, identity/score, pose and capture time; thin gray boxes show truth.

![Masked video before verification](../build/video_verification_20261113/masked_3_sync/stills/unverified_first.png)

![Two registered people verified; the stranger remains unverified](../build/video_verification_20261113/masked_3_sync/stills/verified_andrew_clyde.png)

Review the [annotated synchronous masked MP4](../build/video_verification_20261113/masked_3_sync/annotated.mp4)
or [annotated worker MP4](../build/video_verification_20261113/masked_3_worker/annotated.mp4).
A still alone does not establish track continuity; the frame CSVs and truth audit
provide the continuity evidence.

## Tracker and lifecycle regressions

`test_detect_units` exited 0 with `ALL TESTS PASSED`. Relevant checks cover:

- Two moving faces keep their IDs for 20 frames.
- Short disappearance preserves the track ID.
- Tracks are removed after more than 15 missed frames.
- Motion prediction preserves an ID during movement of 40 pixels per frame.

Evidence: [tracker unit log](../build/video_verification_20261113/tracker_units.log).

The additional command `test_pipeline models/permissive.ini --async --sqlite --size
640x480` exited 1 with **32 failures**. This was not an MP4 playback test: it renders
its own scenes in memory. Inspection found fixed coordinates such as `(640, 540)`
and `(1030, 540)` in those scenes; several actors are outside the 640x480 canvas.
Registration-from-track and later identity/card checks then fail because suitable
face crops/tracks were never established. This supports a fixture-layout problem;
it is not evidence that all 32 failures are separate engine defects. The suite's
separate size-aware walk checks did pass at the smaller resolution.

Do not use that complete fixed-layout suite at 640x480 as an all-green acceptance
check until the fixture coordinates are scaled. Its log is retained here:
[low-resolution regression log](../build/video_verification_20261113/pipeline_worker_sqlite.log).
No engine behavior or fixture assertions were weakened to hide those failures.

The repeat at the default **1920x1080**, using
`test_pipeline models/permissive.ini --async --sqlite`, exited **0** with
**ALL TESTS PASSED (0 failures)**. It verified:

- Registration from portraits and from a currently tracked face crop.
- Stable IDs for an approaching person and two simultaneously tracked people.
- Pose-gated crop creation, at least one-second crop spacing, no new crops after
  verification, and continued crop refresh for an unverified stranger.
- Correct identification of registered people and rejection of strangers.
- Renaming a person, immediate propagation of the name to a verified track,
  persistent deletion, and agreement between the database file and memory.
- The separately sized walks at 640x480, 1280x720, 1920x1080, 1600x1200 and 2560x1440.
- A stranger reusing a previously verified person's track does not inherit their
  verification after the disappearance/reappearance scenario.
- Card ownership, unknown/wrong/deleted cards, independent face/card counts,
  and return to ordinary gallery search after the card check.
- Worker latency assertions, ignoring a stale result after `reset_tracking()`,
  and safe destruction of a pipeline with a busy worker.

Evidence: [Full HD worker/SQLite regression log](../build/video_verification_20261113/pipeline_worker_sqlite_fhd.log).
The Full HD repeat supports the fixed-layout diagnosis; it does not repair the
640x480 fixture.

### Negative control for the verifier

Remove the final frame's rows from the three-person truth file, then run the
same complete MP4 with `--truth` selecting that shortened file. The corrected
verifier exits **1** and reports both a frame-count mismatch and missing truth
for a decoded frame. This expected rejection passed the negative-control test.

The first negative-control attempt exposed a verifier defect: `truth[index]`
inserted empty entries and inflated `truth.size()` during playback, masking the
mismatch. The verifier now snapshots the expected count before playback and
counts frames without truth. All six MP4 cases were rerun after the fix and still
passed. Their final metrics and audit are the values reported above.

Evidence: [negative-control log](../build/video_verification_20261113/negative_truncated_truth.log)
and [shortened truth file](../build/video_verification_20261113/truncated_truth.csv).

## Reproduce the sequence

Run from the repository root with model files, portraits and source clips present.
Choose a new output root if retaining this evidence. The added `--artifacts` option
isolates verifier outputs, and `--portraits` / `--portrait-file` select the masked
source without replacing originals.

```powershell
& 'C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/MSBuild/Current/Bin/MSBuild.exe' _test.sln /m /p:Configuration=Release /p:Platform=x64
$runner = 'build/vs2019/Release/_test.exe'
$root = 'build/video_verification_20261113'

& $runner gen_video "$root/masked_3_source" 8 15 3 0 2 --portraits data/portraits_masked --portrait-file 117th.jpg.png

& $runner check_video models/permissive.ini data/videos/fhd_3people/video.mp4 --artifacts "$root/existing_3_sync" --out "$root/existing_3_sync/annotated.mp4"
& $runner check_video models/permissive.ini data/videos/fhd_3people/video.mp4 --async --artifacts "$root/existing_3_worker" --out "$root/existing_3_worker/annotated.mp4"
& $runner check_video models/permissive.ini data/videos/fhd_5people/video.mp4 --artifacts "$root/existing_5_sync" --out "$root/existing_5_sync/annotated.mp4"
& $runner check_video models/permissive.ini data/videos/fhd_cam_5people/video.mp4 --artifacts "$root/camera_5_sync" --out "$root/camera_5_sync/annotated.mp4"
& $runner check_video models/permissive.ini "$root/masked_3_source/video.mp4" --portraits data/portraits_masked --portrait-file 117th.jpg.png --artifacts "$root/masked_3_sync" --out "$root/masked_3_sync/annotated.mp4"
& $runner check_video models/permissive.ini "$root/masked_3_source/video.mp4" --async --portraits data/portraits_masked --portrait-file 117th.jpg.png --artifacts "$root/masked_3_worker" --out "$root/masked_3_worker/annotated.mp4"

python scripts/audit_video_verification.py $root

& $runner test_detect_units
& $runner test_pipeline models/permissive.ini --async --sqlite
```

Capture each command's stdout/stderr and exit status if repeating the report.
Pipeline regressions use their standard scratch database, not the dashboard's
user database; they also refresh generated frames in
`data/videos/approach_registered`. Portrait originals are not modified.

## Evidence files and limits

[Evidence directory](../build/video_verification_20261113/) contains each run's
`.log`, plus `people.csv`, `frames.csv`, `annotated.mp4` and `stills/` under each
case folder. [Independent audit](../build/video_verification_20261113/independent_audit.json)
contains recomputed per-person statistics.
[Input manifest](../build/video_verification_20261113/input_manifest.json) records
SHA-256 hashes of MP4 inputs, profile, runner binary and verifier/generator sources.
These `build/` outputs are local artifacts; retain them with the report when sharing.

The verifier was tightened to count process failures, require a complete decoded
frame count, prevent reuse of a detection for two truth faces, compare every
verified identity directly with truth, count ID transitions, and require exactly
one track ID. Engine implementations were unchanged.

This is controlled, generated footage of separated, mostly frontal people. It
does not validate tracking through crossing people, severe occlusion, long
absence, real yaw/pitch rotation, difficult live-camera conditions or spoof/liveness
attacks. The same underlying portraits drive enrollment and generated actors, so
these are pipeline-function checks rather than an independent accuracy benchmark.
Only the permissive YuNet/SFace profile was evaluated here.
