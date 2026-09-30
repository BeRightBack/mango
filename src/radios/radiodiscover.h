/*
 * Strawberry Music Player
 * Copyright 2026, Steven Pinel <steven_pinel@hotmail.com>
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

#ifndef RADIODISCOVER_H
#define RADIODISCOVER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include "includes/shared_ptr.h"

class NetworkAccessManager;
class QNetworkReply;
class QProcess;

// Locates a working public stream for a station that is no longer served by
// the URL a directory handed out.
//
// Some stations have left the platform the directory still lists, so no URL on
// that host can play them and no path rewrite can help. Those are the stations
// this class resolves, by deriving candidate stream URLs from the station's call
// sign and proving each one decodes.
//
// Deriving candidates happens in tiers, cheapest and most deterministic first:
//
//   1. streamtheworld PLS, keyed by call sign. This resolves most Canadian
//      stations that moved to that platform.
//   2. Call sign variants, because hosts disagree on the -ADP, _SC and AMAAC
//      endings.
//   3. leanstream sibling paths, including the -MP3 form, which often still
//      serves audio when the AAC path on the same host is dead.
//
// Every candidate must decode before it is reported. A URL that merely returns
// HTTP 200 is worthless here: the leanstream CDN answers 200 with a
// self-referential playlist for any path at all, so status codes prove nothing.
// Decodability is the only acceptable evidence, which is why this class runs
// gst-launch-1.0 against each candidate.
//
// Identity matters as much as decodability. A candidate that plays but belongs
// to a different station is worse than no answer, because it silently
// substitutes the wrong station. The directory name tier is therefore only
// reached when the query carries a call sign or a frequency that can be checked
// against the result.
class RadioDiscover : public QObject {
  Q_OBJECT

 public:
  explicit RadioDiscover(const SharedPtr<NetworkAccessManager> &network, QObject *parent = nullptr);
  ~RadioDiscover() override;

  // Resolves a leanstream station slug to a verified stream URL, or returns
  // false when no candidate could be proven to decode.
  //
  // done is always invoked exactly once, on the event loop thread.
  void Discover(const QString &slug,
                const std::function<void (const QUrl &)> &done);

  void Abort();

  // Test seams. Exposed so the behaviour can be exercised without a network.
  static QString NormalizeCallsign(const QString &callsign);
  static QList<QString> ExtractPlsEntries(const QByteArray &data);
  static bool SameStation(const QString &result_name, const QString &wanted);
  static bool IsMalformedMaster(const QByteArray &data);

 private:
  void TryTier(const QList<QUrl> &candidates, int index, const QUrl &slug_url,
               const std::function<void (const QUrl &)> &done);

  // Fetches a PLS or an HLS master, then either takes the media URL out of the
  // PLS or schedules a decode test for it.
  void FetchThenVerify(const QUrl &pls_url, int depth,
                       const std::function<void (const QUrl &)> &done);

  // Runs the candidate through GStreamer. Asynchronous: the pipeline is
  // stopped once it has produced audio, so this does not block the interface.
  void DecodeTest(const QUrl &candidate, const std::function<void (bool)> &done);

  void Emit(const std::function<void (const QUrl &)> &done, const QUrl &url);

  SharedPtr<NetworkAccessManager> network_;
  QList<QNetworkReply*> replies_;
  QList<QProcess*> processes_;
  QHash<QString, QUrl> cache_;

  static QList<QUrl> StreamtheworldCandidates(const QString &callsign);
  static QList<QUrl> LeanstreamCandidates(const QString &callsign);

  // In-flight discovery state, one chain at a time per call.
  int pending_level_{0};
};

#endif  // RADIODISCOVER_H
