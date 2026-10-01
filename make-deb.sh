#!/usr/bin/env bash
#
# Build a Debian package for Mango Music Player.
#
# Uses dpkg-deb directly rather than debhelper, which keeps the build working on
# a machine that has no packaging tools installed.
#
# The Depends list is derived from the linked shared libraries of the binary that
# is actually being packaged, mapped to owning packages with dpkg -S, so it
# cannot drift from reality the way a hand-maintained list does.
#
# Usage:  ./make-deb.sh [version]
#         ./make-deb.sh 2.0.2
#
# Requires: the project to be built (build/mango exists).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$ROOT/build/mango"
STAGE="$ROOT/build/deb/mango"
VERSION="${1:-}"

if [[ ! -x "$BINARY" ]]; then
  echo "error: $BINARY not found or not executable. Build it first:" >&2
  echo "         cmake -B build && cmake --build build -j\$(nproc)" >&2
  exit 1
fi

if [[ -z "$VERSION" ]]; then
  # The binary prints "Mango Music Player 2.0.1"; take the last field.
  VERSION="$("$BINARY" --version 2>/dev/null | awk '{print $NF}')"
fi
if [[ ! "$VERSION" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
  echo "error: could not determine a version (got '$VERSION'). Pass one: ./make-deb.sh 2.0.1" >&2
  exit 1
fi

ARCH="$(dpkg --print-architecture)"
PKG="mango_${VERSION}_${ARCH}"

echo "==> Mango $VERSION ($ARCH)"

# ---------------------------------------------------------------- dependencies
# Map every shared library the binary needs to the Debian package that owns it.
#
# dpkg -S matches on substrings across all installed files, so it must only ever
# be given a fully resolved absolute path. Given a bare soname it will happily
# answer with an unrelated package that happens to bundle its own copy of the
# same library name.
echo "==> resolving runtime dependencies"
mkdir -p "$ROOT/build/deb"
: > "$ROOT/build/deb/deps.raw"
missing=0
while read -r so; do
  real="$(readlink -f "/usr/lib/x86_64-linux-gnu/$so" 2>/dev/null || true)"
  if [[ -z "$real" || ! -e "$real" ]]; then
    real="$(find /usr/lib /lib -name "$so" -print -quit 2>/dev/null || true)"
  fi

  # "diversion by ... " lines describe a diversion, not an owner, so drop them.
  # The package name may carry an :arch qualifier; strip it.
  pkg="$(dpkg -S "$real" 2>/dev/null | grep -v '^diversion ' \
          | awk -F': ' '{print $1}' | cut -d: -f1 | head -1 || true)"

  # A handful of libraries are shipped inside another package's payload without
  # being individually owned, so dpkg -S reports nothing for them. Map them by
  # hand, verified against the installed package contents.
  case "$so" in
    libEGL.so.1)              pkg="libegl1" ;;     # glx-diversions target
    libpulsecommon-*.so)       pkg="libpulse0" ;;   # private to libpulse0
    libpxbackend-1.0.so)       pkg="libproxy1v5" ;; # libproxy's GIO module
  esac

  if [[ -z "$pkg" ]]; then
    echo "    UNRESOLVED: $so (${real:-not found})" >&2
    missing=1
  else
    echo "$pkg" >> "$ROOT/build/deb/deps.raw"
  fi
done < <(ldd "$BINARY" | awk '{print $1}' | grep '^lib' | sort -u)

if [[ $missing -ne 0 ]]; then
  # Fail loudly rather than shipping a package with a silently incomplete
  # dependency list, which is how the wrong provider ends up baked in.
  echo "error: some libraries could not be mapped to a package. Add a case above." >&2
  exit 1
fi

sort -u "$ROOT/build/deb/deps.raw" -o "$ROOT/build/deb/deps.raw"

# -dev packages are build-time only and must never appear in a runtime Depends.
if grep -q -- '-dev$' "$ROOT/build/deb/deps.raw"; then
  echo "error: a development package leaked into the dependency list:" >&2
  grep -- '-dev$' "$ROOT/build/deb/deps.raw" >&2
  exit 1
fi

# Drop the base libraries; apt always has these and listing them adds nothing.
grep -vxE 'libc6|libgcc-s1|libstdc\+\+6|zlib1g' "$ROOT/build/deb/deps.raw" \
  > "$ROOT/build/deb/deps.txt" || true

DEPEND="$(tr '\n' ',' < "$ROOT/build/deb/deps.txt" | sed 's/,$//')"
echo "    $(wc -l < "$ROOT/build/deb/deps.txt") runtime packages"

# ------------------------------------------------------------------- staging
echo "==> staging"
# Clean the staging tree itself, not a path derived from the package name, or
# files removed from this script keep shipping from a previous run.
rm -rf "$STAGE" "$ROOT/build/deb/$PKG"
mkdir -p "$STAGE"/DEBIAN
mkdir -p "$STAGE"/usr/bin
mkdir -p "$STAGE"/usr/share/applications
mkdir -p "$STAGE"/usr/share/doc/mango
mkdir -p "$STAGE"/usr/share/man/man1

install -m 0755 "$BINARY" "$STAGE/usr/bin/mango"

# Desktop entry, generated here rather than copied so the package does not depend
# on a file that only exists in the developer's home directory.
cat > "$STAGE/usr/share/applications/mango.desktop" <<'DESKTOP'
[Desktop Entry]
Version=1.0
Type=Application
Name=Mango Music Player
GenericName=Music Player
Comment=Plays music
Exec=mango %U
TryExec=mango
Icon=mango
Terminal=false
Categories=AudioVideo;Player;Qt;Audio;
Keywords=Audio;Player;Radio;Music;
MimeType=x-content/audio-player;audio/flac;audio/ogg;audio/vorbis;audio/aac;audio/mp4;audio/mpeg;audio/mpegurl;audio/x-flac;audio/x-vorbis;audio/x-speex;audio/x-wav;audio/x-wavpack;audio/x-ape;audio/x-mp3;audio/x-mpeg;audio/x-mpegurl;audio/x-ms-wma;audio/x-musepack;
StartupNotify=true
DESKTOP

# Icons. Prefer the ones installed in the system icon theme; fall back to the
# sizes the application resource file was built with.
for src in ~/.local/share/icons/hicolor/*/apps/mango.png \
           ~/.local/share/icons/hicolor/*/apps/mango.svg; do
  [[ -e "$src" ]] || continue
  size="$(basename "$(dirname "$(dirname "$src")")")"
  mkdir -p "$STAGE/usr/share/icons/hicolor/$size/apps"
  install -m 0644 "$src" "$STAGE/usr/share/icons/hicolor/$size/apps/"
done
echo "    $(find "$STAGE/usr/share/icons" -type f | wc -l) icons"

# Documentation.
install -m 0644 "$ROOT/COPYING" "$STAGE/usr/share/doc/mango/copyright"
[[ -f "$ROOT/README.md" ]] && install -m 0644 "$ROOT/README.md" "$STAGE/usr/share/doc/mango/README.md"

cat > "$STAGE/usr/share/doc/mango/changelog" <<CHANGELOG
mango ($VERSION) unstable; urgency=medium

  * Mango Music Player $VERSION.
    Custom port of Strawberry Music Player 1.2.30 which repairs unusable HLS
    radio streams from the leanstream CDN and discovers replacement streams at
    runtime. Licensed under the GNU General Public License version 3 or later;
    Strawberry itself is credited in the About dialog.

 -- Steven Pinel <steven_pinel@hotmail.com>  $(date -R)
CHANGELOG
# Generated files inherit the caller's umask; pin the mode so the package does not
# ship world-writable or group-writable files depending on who built it.
chmod 0644 "$STAGE/usr/share/applications/mango.desktop"

gzip -9n "$STAGE/usr/share/doc/mango/changelog"
chmod 0644 "$STAGE/usr/share/doc/mango/changelog.gz"

# Man page.
cat > "$STAGE/usr/share/man/man1/mango.1" <<'MAN'
.TH MANGO 1 "2026" "mango $VERSION" "User Commands"
.SH NAME
mango \- music player and collection organiser
.SH SYNOPSIS
.B mango
[\fIOPTION\fR]... [\fIURL\fR]...]
.SH DESCRIPTION
.B Mango
plays audio files and internet radio streams, and organises a local music
collection into albums and artists.
.PP
Radio station streams found through the built\-in Radio Browser directory are
checked before they are offered, and streams that are known to be broken are
repaired or replaced automatically.
.SH PLAYER OPTIONS
.TP
.BR \-p ", " \-\-play
Start the playlist currently playing.
.TP
.BR \-t ", " \-\-play\-pause
Play if stopped, pause if playing.
.TP
.BR \-s ", " \-\-stop
Stop playback.
.TP
.BR \-f ", " \-\-next
Skip forwards in the playlist.
.TP
.BR \-r ", " \-\-previous
Skip backwards in the playlist.
.TP
.BR \-v ", " \-\-volume " \fIVALUE\fR"
Set the volume to \fIVALUE\fR percent.
.SH PLAYLIST OPTIONS
.TP
.BR \-c ", " \-\-create " \fINAME\fR"
Create a new playlist with the given files.
.TP
.BR \-a ", " \-\-append
Append files or URLs to the current playlist.
.TP
.BR \-l ", " \-\-load
Load files or URLs, replacing the current playlist.
.TP
.BR \-i ", " \-\-play\-playlist " \fINAME\fR"
Play the named playlist.
.SH OTHER OPTIONS
.TP
.BR \-v ", " \-\-version
Print version information and exit.
.TP
.BR \-h ", " \-\-help
Print a summary of the available options and exit.
.SH FILES
.TP
.I ~/.config/mango/mango.conf
Settings.
.TP
.I ~/.local/share/mango/mango/mango.db
Collection database.
.SH SEE ALSO
The Mango project: https://frenzyzone.com
.PP
Derived from Strawberry Music Player: https://www.strawberrymusicplayer.org
MAN
gzip -9n "$STAGE/usr/share/man/man1/mango.1"
chmod 0644 "$STAGE/usr/share/man/man1/mango.1.gz"

# -------------------------------------------------------------------- control
echo "==> writing control"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: mango
Version: $VERSION
Section: sound
Priority: optional
Architecture: $ARCH
Depends: $DEPEND
Maintainer: Steven Pinel <steven_pinel@hotmail.com>
Homepage: https://frenzyzone.com
Description: music player and collection organiser with repairing radio streams
 Mango plays audio files and internet radio, and organises a local collection
 into albums and artists.
 .
 It is a custom port of Strawberry Music Player 1.2.30. Stream URLs found
 through the Radio Browser directory are validated before they are offered, and
 stations whose streams are known to be broken are repaired from their master
 playlist or replaced with a working stream discovered at runtime. Every
 candidate must decode successfully before it is accepted, and a station that
 cannot be resolved is reported rather than guessed at.
 .
 Strawberry is credited in the About dialog and retains the GNU General Public
 License version 3; Mango is free software and is not for sale.
CONTROL

# ---------------------------------------------------------------------- build
echo "==> building"
OUT="$ROOT/build/$PKG.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group --build "$STAGE" "$OUT" > /dev/null

echo
echo "    $OUT"
dpkg-deb --info "$OUT" | sed 's/^/    /'
echo
echo "Install with:  sudo apt install ./$PKG.deb"
