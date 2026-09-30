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

#include "radiorepair.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>
#include <QtDebug>

#include "core/networkaccessmanager.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Stations that are no longer served by their directory URL at all. Every
// target below was verified to decode with:
//
//   gst-launch-1.0 playbin uri="<target>" audio-sink=fakesink
//
// and the corresponding directory URL was verified to fail, so each entry is a
// measured replacement rather than a guess.
const QHash<QString, QString> &SubstitutionTable() {
  static const QHash<QString, QString> table = {
      // CHOM 97.7. The leanstream slug serves a self-referential master with
      // no media behind it. The station is live on streamtheworld.
      {u"chomfm"_s, u"https://17553.live.streamtheworld.com/CHOMFM_ADP/HLS/playlist.m3u8"_s},
      // CFRB 1010 Toronto.
      {u"cfrb"_s, u"http://18183.live.streamtheworld.com/CFRBAMAAC_SC"_s},
      // CIMJ 106.1 "Magic 106" Guelph.
      {u"cimjam"_s, u"http://live.leanstream.co/CIMJFM-MP3"_s},
      // CFMB 1280 Montreal.
      {u"cfmb"_s, u"https://evanov.streamb.live/SB00237/playlist.m3u8?args=web_01"_s},
      // CBLA-FM 99.1 Toronto, the continuation of the old CBL 740 call sign.
      {u"cbl"_s, u"https://26183.live.streamtheworld.com/CBLAFM_CBC_SC"_s},
  };
  return table;
}

QRegularExpression SlugExpression() {
  static const QRegularExpression re(QStringLiteral("/rogers/([a-z0-9_-]+)\\.stream"), QRegularExpression::CaseInsensitiveOption);
  return re;
}

QRegularExpression SelfLoopExpression() {
  static const QRegularExpression re(QStringLiteral("/playlist\\.m3u8(\\?.*)?$"), QRegularExpression::CaseInsensitiveOption);
  return re;
}

bool IsLeanstreamHost(const QString &host) {
  return host.endsWith(u"leanstream.co"_s, Qt::CaseInsensitive);
}

bool IsHlsPlaylistUrl(const QUrl &url) {
  return url.path().endsWith(u".m3u8"_s, Qt::CaseInsensitive);
}

}  // namespace

RadioRepair::RadioRepair(const SharedPtr<NetworkAccessManager> &network, QObject *parent)
    : QObject(parent), network_(network) {}

RadioRepair::~RadioRepair() {
  Abort();
}

QString RadioRepair::VerifiedStreamForSlug(const QString &slug) {
  return SubstitutionTable().value(slug.toLower(), QString());
}

QString RadioRepair::SlugForUrl(const QUrl &url) {
  const QRegularExpressionMatch match = SlugExpression().match(url.toString());
  if (!match.hasMatch()) return QString();
  return match.captured(1).toLower();
}

bool RadioRepair::IsMalformedMaster(const QByteArray &data) {

  if (!data.contains("#EXTM3U")) return false;

  // A master that hlsdemux can use always announces a bandwidth for each
  // variant. The broken playlists contain a bare tag with no attributes.
  const bool has_bandwidth = data.contains("BANDWIDTH");
  const bool has_stream_inf = data.contains("#EXT-X-STREAM-INF");

  return has_stream_inf && !has_bandwidth;

}

QUrl RadioRepair::RepairMasterUrl(const QUrl &url) {

  const QString host = url.host();

  // The station may have moved to a different broadcaster entirely, in which
  // case no path on this host will ever work.
  if (IsLeanstreamHost(host)) {
    const QString slug = SlugForUrl(url);
    if (!slug.isEmpty()) {
      const QString replacement = VerifiedStreamForSlug(slug);
      if (!replacement.isEmpty()) return QUrl(replacement);
    }
  }

  // Otherwise the master is merely malformed and the per-bitrate media
  // playlist is still served on this host. Point the player at it directly so
  // the broken master is never parsed.
  QString s = url.toString();
  s.replace(SelfLoopExpression(), QStringLiteral("/48k/playlist.m3u8"));

  return QUrl(s);

}

void RadioRepair::Abort() {
  for (QNetworkReply *reply : replies_) {
    reply->abort();
  }
  replies_.clear();
}

void RadioRepair::RepairChannels(const QList<QUrl> &urls, const DoneCallback &done) {

  if (urls.isEmpty()) {
    done(urls);
    return;
  }

  RepairAt(urls, 0, QList<QUrl>(), done);

}

void RadioRepair::RepairAt(const QList<QUrl> &urls, int index, QList<QUrl> repaired, const DoneCallback &done) {

  if (index >= urls.size()) {
    done(repaired);
    return;
  }

  const QUrl url = urls.at(index);

  // Known-moved stations do not need a probe, the substitution is definitive.
  if (IsLeanstreamHost(url.host())) {
    const QString slug = SlugForUrl(url);
    if (!slug.isEmpty() && !VerifiedStreamForSlug(slug).isEmpty()) {
      const QUrl repaired_url(VerifiedStreamForSlug(slug));
      qDebug() << "RadioRepair: substituted" << url.toString() << "->" << repaired_url.toString();
      repaired.append(repaired_url);
      RepairAt(urls, index + 1, repaired, done);
      return;
    }
  }

  // Only leanstream HLS masters are inspected. Everything else is a different
  // host whose playlists are not affected by the defects handled here, so it
  // is passed through without a network round trip.
  if (!IsLeanstreamHost(url.host()) || !IsHlsPlaylistUrl(url)) {
    repaired.append(url);
    RepairAt(urls, index + 1, repaired, done);
    return;
  }

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader, u"Mozilla/5.0 (X11; Linux x86_64) Mango/2.0"_s);

  QNetworkReply *probe = network_->get(request);
  if (probe == nullptr) {
    // Could not probe. Leave the URL alone rather than guess.
    repaired.append(url);
    RepairAt(urls, index + 1, repaired, done);
    return;
  }

  replies_.append(probe);

  QObject::connect(probe, &QNetworkReply::finished, this,
                   [this, probe, urls, index, repaired, done]() {
    if (replies_.contains(probe)) replies_.removeAll(probe);

    QUrl result = urls.at(index);

    if (probe->error() == QNetworkReply::NoError) {
      const QByteArray data = probe->readAll();
      if (IsMalformedMaster(data)) {
        const QUrl repaired_url = RepairMasterUrl(urls.at(index));
        if (repaired_url != result) {
          qDebug() << "RadioRepair: repaired malformed master" << urls.at(index).toString()
                   << "->" << repaired_url.toString();
          result = repaired_url;
        }
      }
    } else {
      qDebug() << "RadioRepair: could not probe" << urls.at(index).toString() << probe->errorString();
    }

    probe->deleteLater();

    QList<QUrl> next = repaired;
    next.append(result);
    RepairAt(urls, index + 1, next, done);
  });

}
