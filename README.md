# Mango Music Player

A custom port of [Strawberry Music Player](https://github.com/strawberrymusicplayer/strawberry) 1.2.30,
itself a fork of Clementine.

Mango is a music player and collection organizer that plays your own files,
streams internet radio, and reaches streaming services. The direction of this
port is reliability: sources change, directories serve data that no longer
plays, and the player should notice and recover rather than fail quietly.

## What this fork is working on

**Radio streams that the directory breaks.** Radio Browser and the leanstream
CDN both serve HLS master playlists that cannot play. Two distinct defects:

- Stations that moved to a different broadcaster. The CDN answers `HTTP 200`
  for any path on those slugs, but returns a master playlist pointing back at
  itself on port 8000, with a bare `#EXT-X-STREAM-INF` and no `BANDWIDTH`.
  GStreamer's `hlsdemux` rejects the tag, and the variant would loop anyway.
- Stations that are fine on the same host, where the playlists are identical
  except for a valid `BANDWIDTH`.

Because a URL alone cannot tell those apart, blindly rewriting every leanstream
URL is not safe — pointing a healthy station at its `48k` variant breaks it.
Mango fetches the master and repairs only a playlist that is genuinely broken.

**Streams are verified, never assumed.** A URL that returns `HTTP 200` proves
nothing here. Every candidate must decode before it is used, and a station that
cannot be proven to play is reported as unresolved rather than pointed at a
guess. A wrong-but-working stream substitutes a different station silently,
which is worse than failing.

**Identity is checked, not just playback.** A station that plays but belongs to
something else is still wrong. Directory name matches have to agree on call sign
and frequency before they are accepted, so a query for one station cannot return
another that happens to be online.

**Search is scoped.** Radio Browser serves stations worldwide, and discovery
spends a decode probe per candidate. Settings holds a checkable country list,
defaulting to a handful, so a search returns stations that will actually be
listened to and repair is never spent on a filtered-out result.

## Building

Requires Qt 6.4+ and the dependencies declared in `debian/control`:

```bash
sudo apt install build-essential cmake pkg-config \
  qt6-base-dev qt6-base-private-dev qt6-base-dev-tools \
  qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
  libkdsingleapplication-qt6-dev libglib2.0-dev libssl-dev libboost-dev \
  libsqlite3-dev libicu-dev libtag-dev libxkbcommon-dev libasound2-dev \
  libpulse-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libcdio-dev libgpod-dev libmtp-dev libchromaprint-dev libfftw3-dev \
  libebur128-dev libsparsehash-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
```

The binary is `build/mango`. It reports itself as *Mango Music Player* and
keeps its data separate:

| | Mango |
|---|---|
| database | `~/.local/share/mango/mango/mango.db` |
| settings | `~/.config/mango/mango.conf` |

## Tools

| | |
|---|---|
| `tools/find-stream` | Derive and verify a stream for a call sign, in tiers, decoding every candidate. |
| `tools/check-stations` | Report which saved stations are dead or duplicated, and remove them with `--apply`. |

`find-stream` tries streamtheworld PLS endpoints keyed by call sign, then call
sign variants, then leanstream sibling paths, then Radio Browser, then the
station's own site. It resolves three of the five stations the substitution
table covers; the remaining two are not served by a platform derivable from a
call sign, so the table still needs them.

## Known limits

- Substituted streamtheworld URLs are load-balanced across edge hosts, so a
  hardcoded host needs updating over time.
- Stations with no audio anywhere cannot be repaired: `chch` is a television
  station, `chmj` went off air in February 2025.
- A leanstream slug is often not the call sign, so some stations are
  undiscoverable without the table.

## Licence

Mango is free software under the **GNU General Public License v3 or later**,
inherited from Strawberry, which is in turn a fork of Clementine. All upstream
copyright and licence headers are preserved unchanged; that attribution is a
licence requirement, not an oversight.

Strawberry Music Player is Copyright the Strawberry contributors.
Clementine is Copyright David Sansome and contributors.

Released free of charge and not for sale.