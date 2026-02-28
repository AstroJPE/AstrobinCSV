#!/usr/bin/env zsh
# bundle_for_windows.zsh
# Bundles the AstrobinCSV project source tree into a zip file suitable for
# transfer to a Windows machine and opening in Qt Creator.
#
# Usage:  ./bundle_for_windows.zsh [project-root]
#
# If project-root is omitted the script uses the directory it lives in.
# The zip is written to the parent of project-root so it is never included
# in its own archive.

set -euo pipefail

# ── Resolve project root ──────────────────────────────────────────────────
SCRIPT_DIR="${0:A:h}"
PROJECT_ROOT="${1:-${SCRIPT_DIR}}"
PROJECT_ROOT="${PROJECT_ROOT:A}"          # absolute, symlinks resolved

PROJECT_NAME="${PROJECT_ROOT:t}"          # last path component, e.g. AstrobinCSV
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
ZIP_NAME="${PROJECT_NAME}_windows_${TIMESTAMP}.zip"
ZIP_PATH="${PROJECT_ROOT:h}/${ZIP_NAME}"  # one level above project root

echo "Project root : ${PROJECT_ROOT}"
echo "Output zip   : ${ZIP_PATH}"
echo ""

# ── Sanity checks ────────────────────────────────────────────────────────
if [[ ! -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
    echo "ERROR: CMakeLists.txt not found in ${PROJECT_ROOT}" >&2
    exit 1
fi

# ── Build file list ──────────────────────────────────────────────────────
# We collect paths relative to the *parent* of the project root so that the
# zip extracts as  AstrobinCSV/CMakeLists.txt  etc., ready to open in
# Qt Creator's "Open Project" dialog by pointing at AstrobinCSV/CMakeLists.txt
cd "${PROJECT_ROOT:h}"

# Patterns to include (relative to project root, passed to zip as-is)
INCLUDES=(
    "${PROJECT_NAME}/CMakeLists.txt"
    "${PROJECT_NAME}/src"
    "${PROJECT_NAME}/resources"
)

# Patterns to exclude even if they fall under an included directory.
# Qt Creator on Windows will regenerate everything under build/ and .qtc/
EXCLUDES=(
    "*.DS_Store"
    "*/.git/*"
    "*/build/*"
    "*/.qtc/*"
    "*/CMakeFiles/*"
    "*/.cmake/*"
    "*.icns"          # macOS icon — Windows uses .ico; exclude to keep zip clean
    "*.user"          # Qt Creator per-user settings (machine-specific)
    "*.user.*"
)

# Build the zip command
ZIP_CMD=(zip -r "${ZIP_PATH}" "${INCLUDES[@]}")
for pat in "${EXCLUDES[@]}"; do
    ZIP_CMD+=(--exclude "${pat}")
done

# ── Create the archive ───────────────────────────────────────────────────
echo "Creating archive…"
"${ZIP_CMD[@]}"

echo ""
echo "Done.  Archive size: $(du -sh "${ZIP_PATH}" | cut -f1)"
echo ""
echo "Transfer ${ZIP_NAME} to the Windows machine, unzip into"
echo "your QtCreatorProjects directory, then open"
echo "  QtCreatorProjects\\${PROJECT_NAME}\\CMakeLists.txt"
echo "in Qt Creator to load the project."
