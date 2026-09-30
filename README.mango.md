# Mango Music Player

A custom port of [Strawberry Music Player](https://github.com/strawberrymusicplayer/strawberry) 1.2.30,
forked to fix Canadian internet radio playback on the leanstream CDN.

## What this fork fixes

Canadian radio stations served from Rogers' leanstream CDN are broken in two
ways, and neither can be fixed by editing a saved playlist.

**1. Stations that moved to a different broadcaster.** CHOM 97.7 and several
other stations are no longer served by leanstream at all. The CDN answers
`HTTP 200` for any path on those slugs, but returns a master playlist that
points back at itself on port 8000:

```
#EXTM3U
#EXT-X-STREAM-INF:
http://rogers-hls.leanstream.co:8000/rogers/chomfm.stream/playlist.m3u8
```

GStreamer's `hlsdemux` rejects the tag because it carries no `BANDWIDTH`, and
even if it did not, the variant would loop forever. The reported error is
`Could not update any variant playlist`.

**2. Malformed master playlists on working stations.** A working station on the
same host serves a byte-identical playlist apart from a valid `BANDWIDTH`:

```
#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=47000,CODECS="mp4a.40.5"
https://rogers-hls.leanstream.co/rogers/tor680.stream/48k/playlist.m3u8
```

Because a URL alone cannot tell the two apart, blindly rewriting every
leanstream URL is not safe: pointing a healthy station straight at its `48k`
variant breaks it. `RadioRepair` therefore fetches the master playlist and only
repairs a playlist that is genuinely malformed.

## How it works

`src/radios/radiorepair.{h,cpp}` is called from
`RadioBrowserService::SearchReply`, at the point where a directory result
becomes a playable stream. A station re-added from Radio Browser is therefore
repaired automatically, with no database edits and no proxy.

- A station whose slug is in the substitution table is replaced with a stream
  that was verified to decode.
- Any other leanstream HLS master is fetched and inspected. If it lacks a
  `BANDWIDTH` attribute it is repointed at a real per-bitrate media playlist.
- Everything else is passed through untouched, without a network round trip.

The substitution table contains only streams verified with:

```bash
gst-launch-1.0 playbin uri="<url>" audio-sink=fakesink
```

## Building

Requires Qt 6.4+ and the dependencies declared in `debian/control`. On Debian
or Kali:

```bash
sudo apt install build-essential cmake pkg-config \
  qt6-base-dev qt6-base-private-dev qt6-base-dev-tools \
  qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
  libkdsingleapplication-qt6-dev libglib2.0-dev libssl-dev libboost-dev \
  libsqlite3-dev libicu-dev libtag-dev libxkbcommon-dev libasound2-dev \
  libpulse-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libcdio-dev libgpod-dev libmtp-dev libchromaprint-dev libfftw3-dev \
  libebur128-dev libsparsehash-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The binary is `build/strawberry`. It reports itself as *Mango Music Player*
and keeps its data separate from Strawberry:

| | Mango |
|---|---|
| database | `~/.local/share/mango/mango/mango.db` |
| settings | `~/.config/mango/mango.conf` |

## Known limits

- The substitution table covers the stations that were verified. A station that
  has moved and is not listed will still fail, and needs its real stream found
  and added to the table.
- Substituted streamtheworld URLs are load-balanced across edge hosts, so a
  hardcoded host may need updating over time.
- Stations with no audio anywhere (`chch` is a television station, `chmj` went
  off air in February 2025) cannot be repaired.

## Licence

Mango is free software under the **GNU General Public License v3 or later**,
inherited from Strawberry, which is in turn a fork of Clementine. All upstream
copyright and licence headers are preserved unchanged; that attribution is a
licence requirement, not an oversight.

Strawberry Music Player is Copyright the Strawberry contributors.
Clementine is Copyright David Sansome and contributors.
