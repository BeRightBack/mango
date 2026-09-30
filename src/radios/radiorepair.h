/*
 * Strawberry Music Player
 * Copyright 2026, Malte Zilinski <malte@zilinski.eu>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef RADIOREPAIR_H
#define RADIOREPAIR_H

#include <functional>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "includes/shared_ptr.h"

class NetworkAccessManager;
class QNetworkReply;

// Repairs broken HLS stream URLs coming out of radio station directories.
//
// The leanstream CDN serves some stations with an unusable master playlist:
//
//   #EXTM3U
//   #EXT-X-STREAM-INF:
//   http://<host>:8000/<path>/playlist.m3u8
//
// Two defects are present at once. The EXT-X-STREAM-INF tag carries no
// BANDWIDTH or CODECS attributes, which GStreamer's hlsdemux rejects outright.
// The variant it lists points back at the same master, so even if the tag were
// accepted the demuxer would loop and report
// "Could not update any variant playlist".
//
// A URL alone cannot tell a broken station from a working one, because working
// stations on the same host are byte-for-byte identical except that they carry
// a valid BANDWIDTH. Blindly rewriting every leanstream URL is therefore not
// safe: pointing a healthy station straight at its 48k variant breaks it.
//
// So the master playlist is fetched and inspected. Only a playlist that is
// actually malformed is repaired.
//
// A separate class of failure cannot be repaired by rewriting a path: the
// station left the platform entirely and no URL on the original host serves
// audio any more. Those are listed in an explicit substitution table, where
// every entry is a stream verified to decode.
class RadioRepair : public QObject {
  Q_OBJECT

 public:
  explicit RadioRepair(const SharedPtr<NetworkAccessManager> &network, QObject *parent = nullptr);
  ~RadioRepair() override;

  // The verified replacement stream for a leanstream station slug, or an empty
  // string when the slug is unknown or its audio is still served.
  static QString VerifiedStreamForSlug(const QString &slug);

  // The station slug in a leanstream URL, or an empty string.
  static QString SlugForUrl(const QUrl &url);

  // Asynchronously inspects each HLS master playlist and calls done() with the
  // channels, repaired where needed. Channels that are not HLS, or that are
  // already valid, are passed through untouched.
  //
  // done is invoked on the event loop thread once every probe has completed,
  // or immediately when there is nothing to probe.
  using DoneCallback = std::function<void (const QList<QUrl> &)>;

  void RepairChannels(const QList<QUrl> &urls, const DoneCallback &done);

  void Abort();

 private:
  // Walks the list one entry at a time, probing each HLS master in turn. The
  // continuation is a plain lambda rather than a slot so that no std::function
  // appears in a signature the Qt meta-object system has to parse.
  void RepairAt(const QList<QUrl> &urls, int index, QList<QUrl> repaired, const DoneCallback &done);

  // True when the playlist is an HLS master that hlsdemux will reject.
  static bool IsMalformedMaster(const QByteArray &data);

  // Builds a repaired URL for a malformed leanstream master.
  static QUrl RepairMasterUrl(const QUrl &url);

  SharedPtr<NetworkAccessManager> network_;
  QList<QNetworkReply*> replies_;
};

#endif  // RADIOREPAIR_H
