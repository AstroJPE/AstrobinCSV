# How AstrobinCSV Processes a WBPP Log File

This document describes the complete step-by-step process the app uses to
parse a PixInsight WBPP log file and produce each column in the acquisition
table.

---

## Step 1 — Parse the WBPP Log File

The app opens the WBPP `.log` file and scans it line by line looking for
`* Begin integration of Light frames` or `* Begin fast integration of Light frames`
markers. Each such marker starts a new integration block. For each block the
parser reads:

- **Filter** — from the `Filter :` line (e.g. `Filter : H`)
- **Exposure** — from the `Exposure :` line (e.g. `Exposure : 300.00s`)
  → produces the **duration** column
- **Binning** — from the `BINNING :` line → produces the **binning** column
- **Target name** — from the `Keywords : [...]` line, by searching for any
  keyword listed in the user's Target Keywords list (e.g. `OBJECT`, `TARGET`).
  If the Target Keywords list is empty, this is skipped and the target name
  will come from the XISF headers in Step 2 instead.
- **List of registered `.xisf` files** — from the `II.images` or `FI.targets`
  array in the block. Each entry is a full path to a registered `.xisf` frame.
  These paths are stored as a list on the group.

Each integration block becomes one internal `AcquisitionGroup` object. A
three-night session with three filters produces nine `AcquisitionGroup` objects
(one per night per filter).

---

## Step 2 — Read XISF Headers

For each `AcquisitionGroup`, the app iterates over every registered `.xisf`
file path collected in Step 1. For each file it reads only the XML header
block — the first 16 bytes give the header length, and only that many bytes
are read. The pixel data that makes up the bulk of the file is never touched.
From the XML header it extracts the following FITS keywords:

- **`DATE-LOC`** — the local date and time the frame was captured. Twelve
  hours are subtracted so that frames taken after midnight are attributed to
  the previous calendar date (the observing night they belong to). The
  resulting date → produces the **date** column
- **`GAIN`** — the camera gain → produces the **gain** column
- **`SET-TEMP`** — the camera sensor set temperature → produces the
  **sensorCooling** column
- **`FILTER`** — the filter name from the camera's acquisition software. If
  present, this overrides the filter name read from the log in Step 1
- **`OBJECT`** — the target name from the camera's acquisition software. If
  present, and if no Target Keyword match was found in Step 1, this overrides
  the target name
- **`AMBTEMP`** — the ambient (outside) temperature → produces the
  **temperature** column (averaged across all frames in the group for the
  same date)

If a registered `.xisf` file cannot be found at its original path (common
when the log was created on a different machine), the app searches for it
automatically in the `../registered/` directory relative to the log file. If
still not found, the user is prompted to locate the directory.

All per-frame values (date, gain, sensor temperature, ambient temperature) are
stored in per-frame lists on the `AcquisitionGroup`.

---

## Step 3 — Parse Calibration Blocks

The app re-reads the same WBPP log file(s) looking for two types of blocks:

### Light calibration blocks (`* Begin calibration of Light frames`)

Each block records which master calibration files were used to calibrate that
group of light frames:

- `IC.masterDarkPath` — path to the master dark
- `IC.masterFlatPath` — path to the master flat
- `IC.masterBiasPath` — path to the master bias (if used directly)

It also collects the `Calibration frame N: input ---> output_c.xisf` summary
lines that appear after the block ends. These `_c.xisf` output filenames are
the key that links a calibrated frame back to its calibration block.

### Flat calibration + integration block pairs (`* Begin calibration of Flat frames` / `* Begin integration of Flat frames`)

Each pair records which master bias was used to calibrate the flat frames, and
what master flat file was produced. This builds a map of
`master flat path → master bias path`, which is needed because the light
calibration block does not always record the bias directly — when
`IC.masterBiasEnabled = false` the bias was used only to calibrate the flats,
not the lights directly.

---

## Step 4 — Match Calibration Blocks to Groups and Read Frame Counts

For each `AcquisitionGroup`, the app takes the registered `.xisf` filenames
from Step 1, strips any `_r` suffix and adds `_c` to derive the expected
calibrated output filename (e.g. `frame_r.xisf` → `frame_c.xisf`). It then
looks up that filename in the index of calibrated output paths collected in
Step 3 to find which calibration block was used for this group.

Once the matching calibration block is found, the app physically opens each
master `.xisf` file and reads its XML header to extract the frame count from
the processing history. The count is stored as follows:

- The master flat frame count → produces the **flats** column
- The master dark frame count → produces the **darks** column
- The master bias frame count (looked up via the flat → bias map from Step 3,
  or directly from the calibration block) → produces the **bias** column

If a master file cannot be found at its original path (again common across
machines), the app searches the `../master/` directory relative to the log
file, then any previously located master directories, and finally prompts the
user to locate the directory.

---

## Step 5 — Combine Groups and Apply Grouping Strategy

All `AcquisitionGroup` objects from all loaded log files are combined. Groups
with the same target name and filter are merged together into a combined set.
Within each combined set, duplicate frames (the same filename appearing in
multiple groups) are removed.

The combined frames are then split into rows according to the selected
**Row Grouping** strategy:

- **One row per date** — all frames from the same target, filter, and
  observing date are combined into one row. The number of frames →
  **number** column. Gain and sensor temperature are taken from the first
  resolved frame. The flat, dark, and bias counts are taken from the first
  group in the combined set (these counts are attached to the group as a
  whole in Step 4, not per-frame).
- **One row per date + gain + temp** — same as above but frames are also
  separated by gain and sensor cooling temperature, so a night where the gain
  was changed mid-session would produce two rows. Flat, dark, and bias counts
  are again taken from the first group in the combined set.
- **Collapsed** — all frames for the same target and filter are combined into
  a single row regardless of date, with the earliest date shown and the total
  frame count. Flat, dark, and bias counts are taken from the first group in
  the combined set.

---

## Step 6 — Apply Location, Filter Mapping, and Produce Final Columns

After the rows are built:

- **filter** column — the AstroBin numeric filter ID is looked up from the
  filter name (from the XISF `FILTER` keyword or the log `Filter :` line)
  using the mappings configured in Manage Filters
- **filterName** column — the human-readable AstroBin filter name from the
  same mapping (display only — not exported to CSV)
- **bortle** and **meanSqm** columns — applied from the selected Location
  configured in Manage Locations
- **temperature** column — the average of all `AMBTEMP` values from the
  resolved frames that belong to this row's date group
- **Group** column — a label constructed as `"Target / Filter / Date"`
  (display only — not exported to CSV)

The **iso**, **fNumber**, **flatDarks**, and **meanFwhm** columns are not
populated automatically — they are available for the user to fill in manually
by double-clicking the cell in the table.
