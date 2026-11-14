# Face authentication dashboard

Open `face_authentication.sln` in VS2019 or VS2015. Right-click **face_dashboard**
and select **Set as Startup Project**, choose Release or Debug | x64, then press F5.
The solution builds the engine library and links it into the dashboard; no Python,
web server or additional UI framework is required. Model DLLs are copied beside it.

From the repository root, `dashboard.bat` launches the VS2019 Release dashboard,
building it first if missing. `dashboard.bat Debug` selects Debug. Executables are
`build/vs2019/<Configuration>/face_dashboard.exe` or `build/vs2015/<Configuration>/face_dashboard.exe`.
The program locates the repository resources from its executable's ancestor folders.

The window can be resized or maximized. Controls, spacing, text and table columns scale
uniformly from one reference layout, preserving their proportions. The photo preview
fits its image without stretching. The initial window fits the monitor's available work
area, and moving between monitors applies the new DPI. Small windows use scrollbars
rather than shrinking text below the minimum readable size; Shift+wheel scrolls horizontally.
Windows 10 per-monitor DPI APIs are resolved at runtime, with fallbacks for older versions.

Run `face_dashboard.exe --layout-self-test` to check actual child-window proportions,
font sizing, column widths and access to the final action button. Results are written to
`build/dashboard_layout_selftest.log`. The check covers six viewport sizes from 1024x768
to 3840x2160 at 96, 120, 144 and 192 DPI using simulated DPI-change events. These checks
do not replace testing on a physical Windows 10 multi-monitor system.

1. Choose the model profile and a database path, then **Open / create**. `.db`,
   `.sqlite` and `.sqlite3` create SQLite databases; `.csv` creates CSV. Existing
   files are recognized by content. The default is `build/dashboard_people.db`.
   Close the database before changing its path or model. Incompatible feature
   dimensions, damaged files and databases locked by another process are refused.
2. **Load photo**, or enter a camera index (starting at 0) and **Capture camera**.
   Capture obtains one frame and releases the camera. Use exactly one visible face.
   The preview shows the image that registration and authentication will use.
3. Enter a unique person ID, name and optional unique RF card number. Click
   **Register face**. Registration extracts the face feature and saves it immediately.
4. Load another image or capture another frame, then **Authenticate face**. Leave
   RF card empty to search all registered people. Supply a card to compare only
   with that card's owner; an unknown card is rejected. Results show the identity,
   similarity score and profile threshold. Person ID and name do not restrict this
   search. This is single-image recognition, without the video pipeline's temporal
   tracking/pose gating or liveness detection.
5. Select a person in the table. **Save name** changes their name. **Save card**
   assigns a card, or removes it when the card field is empty. **Replace face** uses
   the currently previewed image and asks for confirmation. **Delete person** asks
   for confirmation and removes the record. The selected table row determines the
   target of these operations; person IDs remain fixed.
6. **Export backup** writes all records to another CSV or SQLite file (the extension
   determines the format). The open database cannot be used as the backup target.
   To use a backup, close the database and open that file. All successful edits
   persist immediately, and storage failures are reported without accepting the edit.

Operations run sequentially on a worker thread. Controls are disabled during work;
wait for completion before closing. Error messages appear below the preview.
Registration and recognition use the existing AuthPipeline, FeatureStore and matcher
APIs. Original engine sources and command-line tests remain available through `_test.sln`.

Validation commands, run from the repository root after building:

```powershell
& build/vs2019/Release/face_dashboard.exe --self-test | Out-Null
Get-Content build/dashboard_selftest.log
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_dashboard_ui.ps1
```

The self-test uses unique temporary databases under `build/` and a bundled portrait,
then checks CSV and SQLite enrollment, face/card authentication, duplicate and unknown
card rejection, name/face/card updates, backup, reopening and persistent deletion.
The UI test opens a separate empty database, verifies the window and asynchronous
open result, then closes the window and removes its database. Camera hardware is
not exercised by these tests. A single photograph can be recognized, so this
interface should not be treated as proof of live presence.

## Face-only inspection images

Use `data/portraits_masked/<person>/<original-filename>.png` for the visual
walkthrough. This is a separate masked derivative of `data/portraits`; originals
are preserved. Only the central detected face ellipse is retained. Backgrounds,
clothing and surrounding symbols are replaced with neutral RGB (240, 240, 240).
No cultural identity or nationality is inferred or used to select people.

The preprocessing command (after building `msvc/mask_portraits.vcxproj`) is:

```bat
build\vs2019\Release\mask_portraits.exe data\portraits data\portraits_masked models\permissive.ini
```

The output directory must not exist. Each input is accepted only with exactly one
face at least 64 pixels wide/high, landmarks, successful masked feature extraction,
and similarity to the original at or above the profile threshold. Exclusions and
similarities are recorded in `MASK_MANIFEST.csv`. Source attribution is preserved
in the derivative `LICENSES.csv`. This geometric mask does not classify symbols
or remove marks inside the retained face itself. These transformed images change
the evaluation inputs; historical accuracy measurements still refer to originals.
