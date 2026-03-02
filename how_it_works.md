# How AstrobinCSV Processes a WBPP Log File

This document describes the complete step-by-step process the app uses to
parse a PixInsight WBPP log file and produce each column in the acquisition
table.

---

## Step 1 — Parse the WBPP Log File (`PixInsightLogParser`)

The app opens the WBPP `.log` file and scans it line by line looking for
`* Begin integration of Light frames` or `* Begin fast integration of Light frames`
markers. Each such marker starts a new integration block. Blocks whose output
master file begins with `LN_Reference_` are silently skipped — these are Local
Normalization reference integrations, not light frame integration.

For each accepted block the parser reads:

- **Filter** — from the `Filter :` line (e.g. `Filter : H`). Stored on each
  frame; may be overridden by the `FILTER` FITS keyword in Step 2.
- **Exposure** — from the `Exposure :` line (e.g. `Exposure : 300.00s`)
  → produces the **duration** column.
- **Target name** — from the `Keywords : [...]` line, by searching for any
  keyword listed in the user's Target Keywords list (e.g. `TARGET`, `OBJECT`).
  If a match is found, `targetFromLog` is set to true and the value is stored
  as `logTarget` on both the group and each frame. If the Target Keywords list
  is empty, this step is skipped entirely and the target name comes from the
  XISF `OBJECT` header keyword in Step 2.
- **List of registered `.xisf` file paths** — from the `II.images` or
  `FI.targets` array in the block. Each entry is the full path to a
  registered `.xisf` frame produced by WBPP's registration step.

Each accepted integration block becomes one `IntegrationGroup` object, which
carries the group-level fields (`exposureSec`, `logTarget`, `targetFromLog`,
`sourceLogFile`, `sessionIndex`) plus a list of `AcquisitionFrame` objects —
one per registered `.xisf` path.

---

## Step 2 — Resolve XISF Headers (`FrameResolveWorker`, stage 1)

Frame resolution runs on a background thread. For each `AcquisitionFrame` in
each `IntegrationGroup`, the worker attempts to read the frame's XISF header.
Only the XML header block is read — the first 16 bytes give the header length,
and only that many bytes are fetched. The pixel data is never touched.

From the XML header the following FITS keywords are extracted:

- **`DATE-LOC`** — local capture date/time. Twelve hours are subtracted so
  that frames taken after midnight are attributed to the previous calendar
  date (the observing night). → produces the **date** column.
- **`GAIN`** — camera gain → produces the **gain** column.
- **`SET-TEMP`** — sensor set temperature → produces the **sensorCooling**
  column.
- **`FILTER`** — filter name from the acquisition software. If present,
  overrides the filter name read from the log in Step 1.
- **`OBJECT`** — target name from the acquisition software. Stored on the
  frame; used as `logTarget` if no Target Keyword match was found in Step 1
  (i.e. `targetFromLog` is false).
- **`AMBTEMP`** — ambient temperature → contributes to the **temperature**
  column (averaged across all frames sharing the same row in Step 5).
- **`XBINNING`** — horizontal binning factor → produces the **binning**
  column.

### File location strategy

If a registered `.xisf` file is not found at its original path (common when
the log was produced on a different machine), the worker searches using a
tiered fallback strategy:

1. **Primary cache** — exact directories where a registered frame was
   previously found in this session.
2. **Secondary cache** — user-supplied directories searched recursively (up to
   four levels deep).
3. **Auto-probe** — the `../registered/` directory relative to the log file,
   searched recursively.
4. **User prompt** — if all automatic methods fail, the worker pauses and
   signals the main thread to display a directory picker. The chosen directory
   is added to the secondary cache for subsequent frames.

If the user cancels a prompt, further prompts for registered frames are
suppressed for the remainder of the current import.

---

## Step 3 — Parse Calibration Blocks (`CalibrationLogParser`)

While the background worker resolves XISF headers, the main thread also
re-reads the same WBPP log file(s) looking for two types of calibration
blocks.

### Light calibration blocks (`* Begin calibration of Light frames`)

Each block records which master calibration files were applied to a group of
light frames:

- `IC.masterDarkEnabled` / `IC.masterDarkPath` — the master dark, if enabled.
- `IC.masterFlatEnabled` / `IC.masterFlatPath` — the master flat, if enabled.
- `IC.masterBiasPath` / `Master bias:` — the master bias path (when used
  directly to calibrate lights).

The `Calibration frame N: input ---> output_c.xisf` summary lines that appear
after the block's `End` marker are also collected. These `_c.xisf` output
filenames are the key that links each calibrated frame back to its calibration
block.

### Flat calibration + integration block pairs

Each `* Begin calibration of Flat frames` / `* Begin integration of Flat frames`
pair records:

- Which master bias was used to calibrate the flat frames.
- Which master flat file was produced by the subsequent integration.

This builds a `flatToBias` map of `master flat path → master bias path`. This
map is needed because when `IC.masterBiasEnabled = false` in the light
calibration block, the bias was used only to calibrate the flats — not the
lights directly — and the light block does not record it.

### External flat detection

After all log files are parsed, the app checks whether any master flat path
referenced in a light calibration block was produced in a *different* WBPP
session (i.e. not found in the `flatToBias` map). If such external flats are
detected, the user is informed that the bias count for those rows cannot be
determined automatically and will be left blank. Loading the session log that
produced those master flats resolves this.

---

## Step 4 — Resolve Calibration Chains (`FrameResolveWorker`, stage 2)

After successfully reading a frame's XISF header (Step 2), the worker
resolves the frame's calibration chain:

1. The registered `.xisf` filename is inspected for a `_c` suffix (e.g.
   `frame_r_c.xisf`). This calibrated basename is looked up in the index of
   `_c.xisf` output paths built in Step 3 to identify the matching
   `CalibrationBlock`.

2. The master dark, flat, and bias paths from the matching block are stored on
   the frame's `FrameCalibration` record.

3. Each master `.xisf` file is physically opened and its XML header is
   scanned to extract the integrated frame count. Three formats are recognised:
   - `<table id="images" rows="N">` in the XML header (current PixInsight).
   - Entity-encoded equivalent (`&lt;table … rows=&quot;N&quot;`) stored in
     a `PixInsight:ProcessingHistory` property attribute.
   - `ImageIntegration.numberOfImages: N` in a FITS `HISTORY` keyword comment
     (older PixInsight versions).

   The counts are stored as:
   - Master flat frame count → **flats** column.
   - Master dark frame count → **darks** column.
   - Master bias frame count (via `flatToBias` chain first, then directly from
     the calibration block) → **bias** column.

### Master file location strategy

As with registered frames, a tiered fallback is used if a master file is not
at its original path:

1. **Primary cache** — previously located master directories (persists across
   multiple Add Log calls within the same app session).
2. **`../master/` sibling** — the `master` directory adjacent to the log
   file's parent, searched recursively.
3. **Primary cache (recursive)** — exact directories from past finds.
4. **Secondary cache (recursive)** — user-supplied directories.
5. **User prompt** — directory picker pauses the worker; the chosen directory
   is added to the secondary cache. Cancelling suppresses further master
   prompts for the remainder of the current import.

### Back-filling calibration data from supplementary logs

After the worker finishes, the main thread performs a back-fill pass over all
previously loaded `IntegrationGroup` objects (i.e. those loaded in earlier
Add Log calls). For any resolved frame that still has missing calibration
counts (`darks`, `flats`, or `bias` < 0), the app looks up its calibrated
basename in the newly-built `basenameToBlock` index. If a match is found, the
missing counts are resolved using the same tiered master-file lookup logic,
with most reads served from the in-memory cache with no further I/O. This
allows loading a supplementary log from a different WBPP session to
retroactively populate calibration data for frames that were imported earlier.

---

## Step 5 — Combine Groups and Apply Grouping Strategy (`rebuildRows`)

`rebuildRows` is called after every import, and also whenever the user changes
the row grouping, location, filter mappings, or target configuration. It
rebuilds the entire table from the in-memory `m_groups` list.

### Target name resolution

For each `IntegrationGroup`, the display target name is determined as follows:

1. If `logTarget` is set on the group (from a Target Keyword match in Step 1),
   it is used directly.
2. Otherwise, the most frequently occurring `logTarget` value across the
   group's resolved frames (from `OBJECT` headers) is used.
3. If still empty, the log filename's base name is used as a fallback.

The raw log target is then mapped through the user's Target Groups
configuration (`AppSettings::astrobinTargetName`) to produce the final
Astrobin target name used for grouping and display.

### Merging groups by target and filter

Groups with the same Astrobin target name and filter are combined into a
shared frame pool. Duplicate frames (the same filename appearing in multiple
groups, which can occur when multiple log files reference the same registered
frames) are removed by filename.

### Row bucketing

The combined frame pool is split into rows according to the selected
**Row Grouping** strategy:

- **One row per date** — frames are bucketed by observing date only. Gain and
  sensor temperature differences within the same date are ignored.
- **One row per date + gain + temp** — frames are bucketed by date, gain, and
  sensor set temperature. A night where the gain or cooling target changed
  mid-session produces separate rows.
- **Collapsed** — all frames for the same target and filter are combined into
  one row regardless of date. The earliest date is shown; frame counts and
  temperatures are summed/averaged across all buckets.

For each row, calibration counts (`darks`, `flats`, `bias`) are taken from
the first resolved frame in the bucket. If frames within the same bucket have
differing calibration counts, a one-time warning is shown (keyed by group
label and strategy so it is not repeated on subsequent rebuilds).

The ambient temperature for each row is the mean `AMBTEMP` across all resolved
frames in that bucket that carry the keyword. If only some frames have
`AMBTEMP`, a one-time warning is shown.

---

## Step 6 — Apply Location, Filter Mapping, and Produce Final Columns

After the rows are built:

- **filter** column — the AstroBin numeric filter ID is looked up from the
  filter name (from the XISF `FILTER` keyword or the log `Filter :` line)
  using the mappings configured in Manage Filters. Unmapped filters are
  highlighted in amber in the table.
- **filterName** column — the human-readable AstroBin filter name from the
  same mapping (display only — not exported to CSV).
- **bortle** and **meanSqm** columns — applied from the selected Location
  configured in Manage Locations.
- **temperature** column — the mean of all `AMBTEMP` values from the resolved
  frames that belong to this row's bucket.
- **Group** column — a label constructed as `"Target / Filter"` (Collapsed)
  or `"Target / Filter / Date"` (per-date strategies). Display only — not
  exported to CSV.

The **iso**, **fNumber**, **flatDarks**, and **meanFwhm** columns are not
populated automatically — they are available for the user to fill in manually
by double-clicking a cell in the table.

User edits to any cell are preserved across rebuilds. Before each rebuild a
snapshot of all currently set field values is taken (keyed by group label);
after the new rows are computed the snapshot is re-applied, so manual edits
survive grouping strategy changes, log additions/removals, and settings
changes. The filter ID column is intentionally excluded from the snapshot so
that a newly configured filter mapping is always reflected immediately.

---

## Debug Logging

An optional structured debug log can be enabled via **Tools → Enable Debug
Logging** before importing a log file. When active, two files are written to
the platform's application data directory:

- `AstrobinCSV_debug_<timestamp>.log` — human-readable record of every regex
  match attempt, file open, decision, and key/value result produced during
  parsing and frame resolution.
- `AstrobinCSV_debug_<timestamp>.json` — machine-readable equivalent for
  automated analysis.

At startup, if debug log files from a previous session are found, the app
offers to delete them.
