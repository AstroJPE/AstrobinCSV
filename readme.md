# AstrobinCSV

A cross-platform desktop application (macOS and Windows) that converts
[PixInsight](https://pixinsight.com) Weighted Batch Pre-Processing (WBPP)
log files into acquisition CSV files suitable for uploading to
[AstroBin](https://www.astrobin.com).

---

## Features

- **Parses WBPP logs** — reads PixInsight WBPP `.log` files and extracts
  integration groups, including filter, exposure, binning, gain, and
  sensor cooling temperature per frame
- **Reads XISF headers** — resolves per-frame metadata (date, gain,
  SET-TEMP, FILTER, OBJECT, AMBTEMP) directly from registered `.xisf`
  file headers
- **Calibration frame counts** — automatically determines the number of
  darks, flats, and bias frames by reading master calibration `.xisf`
  file headers; handles master files from previous sessions or different
  directory structures with a guided directory picker
- **Flexible row grouping** — group acquisition rows by date, by
  date + gain + sensor temperature, or collapse to a single row per
  integration run
- **Filter mapping** — map your local filter names (H, L, R, O3, etc.)
  to AstroBin filter database entries, fetched directly from the AstroBin
  v2 API
- **Location support** — store observing locations with Bortle scale and
  mean SQM values, applied automatically to all rows
- **Target grouping** — combine multiple log targets into a single
  AstroBin target name; supports WBPP Grouping Keywords to keep separate
  integration blocks distinct
- **CSV export and clipboard copy** — export one CSV per target or copy
  to clipboard, with per-column visibility control
- **Multi-session support** — load multiple WBPP log files; calibration
  data from one session automatically fills in missing values (e.g. bias)
  in another
- **Debug logging** — optional session debug log written in both
  human-readable and JSON formats for diagnosing parsing and file
  resolution issues
- **Cross-platform** — builds and runs on macOS (Apple Silicon + Intel
  universal binary) and Windows (MinGW 64-bit)

---

## How it works

For a detailed description of how the app processes log files and produces
each column, see [HOW_IT_WORKS.md](HOW_IT_WORKS.md).

---

## Requirements

Pre-built binaries are available in the
[Releases](../../releases) section of this repository:

- **macOS** — download the `.dmg` file; requires macOS 15 (Sequoia) or later
- **Windows** — download the `.zip` file; requires Windows 10 or later; unzip and double click the AstrobinCSV.exe file in the directory that unzip created.

---

## Requirements to Build from Source

- [Qt 6.5 or later](https://www.qt.io/download) (Core, Gui, Widgets, Network)
- CMake 3.22 or later
- A C++17 compiler (Clang on macOS, MinGW-w64 on Windows)
- OpenSSL 3.x (required on Windows for the AstroBin filter database fetch;
  bundled by macOS via Secure Transport)

---

## Building

### macOS

```bash
git clone https://github.com/<your-username>/AstrobinCSV.git
cd AstrobinCSV
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/macos
cmake --build build/Release
```

The resulting `.app` bundle is at `build/Release/AstrobinCSV.app`.
To create a distributable `.dmg`, use `macdeployqt`:

```bash
/path/to/Qt/6.x.x/macos/bin/macdeployqt \
    build/Release/AstrobinCSV.app -dmg
```

### Windows

Open the project in Qt Creator, select the **Desktop Qt 6.x.x MinGW 64-bit**
kit, and build. To create a deployable directory, run `windeployqt`:

```cmd
windeployqt --openssl-root "C:\Program Files\OpenSSL-Win64" AstrobinCSV.exe
```

---

## Project Structure

```
AstrobinCSV/
├── CMakeLists.txt
├── resources/
│   ├── app.qrc
│   └── icons/
│       ├── AstrobinCSV.icns       # macOS app icon
│       ├── AstrobinCSV.ico        # Windows app icon
│       └── AstrobinCSV.rc         # Windows resource file
└── src/
    ├── main.cpp
    ├── mainwindow.cpp / .h
    ├── acquisitiontableview.cpp / .h
    ├── xisfheaderreader.cpp / .h
    ├── xisfmasterframereader.cpp / .h
    ├── xisfresolveworker.cpp / .h
    ├── filterwebscraper.cpp / .h
    ├── debuglogger.cpp / .h
    ├── dialogs/
    │   ├── aboutdialog
    │   ├── copycsv
    │   ├── debugresultdialog
    │   ├── managefilters
    │   ├── managelocations
    │   └── managetargets
    ├── logparser/
    │   ├── logparserbase.h
    │   ├── pixinsightlogparser
    │   ├── sirillogparser
    │   └── calibrationlogparser
    ├── models/
    │   ├── acquisitiongroup.h
    │   ├── acquisitionrow.h
    │   ├── csvtablemodel
    │   └── targetgroup.h
    └── settings/
        └── appsettings
```

---

## Settings Storage

- **macOS** — `~/Library/Preferences/com.AstrobinCSV.AstrobinCSV.plist`
- **Windows** — `%LOCALAPPDATA%\AstrobinCSV\AstrobinCSV\AstrobinCSV.ini`

---

## Debug Logs

When debug logging is enabled (Tools → Enable Debug Logging), two log
files are written after each import:

- **macOS** — `~/Library/Application Support/AstrobinCSV/AstrobinCSV/`
- **Windows** — `%LOCALAPPDATA%\AstrobinCSV\AstrobinCSV\`

The files are named `AstrobinCSV_debug_<timestamp>.log` (human-readable)
and `AstrobinCSV_debug_<timestamp>.json` (machine-parseable). The app
prompts to clean up old debug logs on startup.

---

## License

This project is licensed under the
[GNU Lesser General Public License v3.0 (LGPL-3.0)](https://www.gnu.org/licenses/lgpl-3.0.html).

You are free to use, modify, and distribute this software under the terms
of the LGPL. If you distribute a modified version of this library, you
must make the source of your modifications available under the same license.
