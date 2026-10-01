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

#include "radiodiscover.h"

#include <memory>

#include <QBuffer>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QtDebug>

#include "core/networkaccessmanager.h"

using namespace Qt::Literals::StringLiterals;

namespace {

const QString kUserAgent = u"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"_s;

// How long a candidate is given to produce audio before it is judged. A live
// stream keeps running, so the pipeline is stopped deliberately rather than
// waiting for it to end.
constexpr int kDecodeProbeMs = 6000;

// Words that identify nothing. Used to decide whether a directory name match is
// meaningful, and filtered before call signs are extracted.
const QSet<QString> &Stopwords() {
  static const QSet<QString> words = {
      u"RADIO"_s, u"STATION"_s, u"FM"_s,    u"AM"_s,     u"THE"_s,      u"OF"_s,
      u"A"_s,     u"AN"_s,       u"AND"_s,  u"LIVE"_s,   u"CLASSIC"_s,  u"ROCK"_s,
      u"POP"_s,   u"JAZZ"_s,     u"COUNTRY"_s, u"NEWS"_s, u"MUSIC"_s,   u"HIT"_s,
      u"HITS"_s,  u"MIX"_s,      u"OLD"_s,  u"NEW"_s,    u"BEST"_s,     u"TOP"_s,
      u"CHANNEL"_s, u"STREAM"_s, u"CANADA"_s, u"CANADIAN"_s, u"TORONTO"_s,
      u"MONTREAL"_s, u"VANCOUVER"_s, u"CALGARY"_s, u"EDMONTON"_s,
      u"OTTAWA"_s, u"WINNIPEG"_s, u"HALIFAX"_s, u"KITCHENER"_s,
      u"SARNIA"_s, u"BARRIE"_s,  u"GUELPH"_s,
  };
  return words;
}

QStringList CallSignTokens(const QString &text) {
  static const QRegularExpression re(QStringLiteral("\\b[A-Z]{3,8}\\b"));
  QStringList out;
  for (const QString &m : text.toUpper().split(u' ', Qt::SkipEmptyParts)) {
    const QString cleaned = QString(m).remove(u'-').remove(u'.').remove(u'_');
    Q_UNUSED(cleaned)
    QRegularExpressionMatch match = re.match(m.toUpper());
    if (match.hasMatch() && !Stopwords().contains(match.captured(0))) {
      out << match.captured(0);
    }
  }
  return out;
}

}  // namespace

RadioDiscover::RadioDiscover(const SharedPtr<NetworkAccessManager> &network, QObject *parent)
    : QObject(parent), network_(network) {}

RadioDiscover::~RadioDiscover() {
  Abort();
}

QString RadioDiscover::NormalizeCallsign(const QString &callsign) {
  // Underscores are significant: streamtheworld serves CBLAFM_CBC at exactly
  // that path, while stripping the underscore yields CBLAFMCBC and a 404.
  QString out;
  for (const QChar c : callsign) {
    if (c.isLetterOrNumber() || c == u'_') out.append(c);
  }
  return out.toUpper();
}

QList<QString> RadioDiscover::ExtractPlsEntries(const QByteArray &data) {
  // A PLS lists several mirrors of the same stream. Any one of them can be
  // down while the others play, so every entry is a candidate.
  static const QRegularExpression re(
      QStringLiteral("^\\s*File\\d+\\s*=\\s*(\\S+)"), QRegularExpression::CaseInsensitiveOption);
  QList<QString> out;
  for (const QByteArray &line : data.split('\n')) {
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(line).trimmed());
    if (m.hasMatch() && !m.captured(1).isEmpty()) {
      out << m.captured(1);
    }
  }
  return out;
}

bool RadioDiscover::IsMalformedMaster(const QByteArray &data) {
  if (!data.contains("#EXTM3U")) return false;
  return data.contains("#EXT-X-STREAM-INF") && !data.contains("BANDWIDTH");
}

bool RadioDiscover::SameStation(const QString &result_name, const QString &wanted) {

  if (result_name.isEmpty() || wanted.isEmpty()) return false;

  // Without a call sign or a frequency there is nothing to compare, and a short
  // generic token identifies no station in particular.
  static const QRegularExpression freq_re(QStringLiteral("\\b\\d{2,3}\\.\\d\\b"));
  const bool wanted_has_freq = freq_re.match(wanted).hasMatch();

  const QStringList rcall = CallSignTokens(result_name);
  const QStringList wcall = CallSignTokens(wanted);

  if (!rcall.isEmpty() && !wcall.isEmpty()) {
    auto agrees = [](const QString &a, const QString &b) {
      if (a == b) return true;
      if (a.startsWith(b) || b.startsWith(a)) return true;
      return a.length() >= 4 && b.length() >= 4 && a.left(4) == b.left(4);
    };
    bool any = false;
    for (const QString &r : rcall) {
      for (const QString &w : wcall) {
        if (agrees(r, w)) any = true;
      }
    }
    if (!any) return false;
    // A shared call sign is not decisive when the frequencies disagree, since a
    // call sign can be reassigned to a different city.
    if (wanted_has_freq) {
      const QRegularExpressionMatch rf = freq_re.match(result_name);
      if (rf.hasMatch() && rf.captured(0) != freq_re.match(wanted).captured(0)) {
        return false;
      }
    }
    return true;
  }

  // A single short token with no frequency is too ambiguous to trust.
  if (!wanted_has_freq && wcall.size() == 1 && wcall.first().length() <= 3) {
    return false;
  }

  return true;

}

QList<QUrl> RadioDiscover::StreamtheworldCandidates(const QString &callsign) {

  const QString base = NormalizeCallsign(callsign);
  if (base.isEmpty()) return {};

  // Suffixes in rough order of likelihood, matching what hosts actually use.
  const QStringList suffixes = {
      QString(), u"-ADP"_s, u"_SC"_s, u"AMAAC"_s, u"AMAAC_SC"_s, u"FM"_s, u"FM-MP3"_s,
  };

  QList<QUrl> out;
  for (const QString &suffix : suffixes) {
    out << QUrl(u"https://playerservices.streamtheworld.com/pls/"_s + base + suffix + u".pls"_s);
  }
  return out;

}

QList<QUrl> RadioDiscover::LeanstreamCandidates(const QString &callsign) {

  const QString base = NormalizeCallsign(callsign);
  if (base.isEmpty()) return {};

  const QStringList hosts = {
      u"https://live.leanstream.co"_s,
      u"http://live.leanstream.co"_s,
      u"https://rogers-hls.leanstream.co"_s,
      u"https://rogers.leanstream.co"_s,
  };
  // The leanstream slug is frequently not the call sign: cimjam serves
  // CIMJFM-MP3, so an FM insertion has to be tried alongside plain suffixes.
  // -MP3 comes first because several stations serve AAC that is dead while the
  // MP3 sibling on the same host still plays.
  QStringList forms;
  if (base.endsWith(u"AM"_s) || base.endsWith(u"FM"_s)) {
    // A slug ending in am/fm is usually the call sign with its band marker in
    // the wrong place: cimjam serves CIMJFM-MP3, not CIMJAM-MP3. Moving the
    // marker to the end produces the form that actually resolves.
    const QString stem = base.left(base.size() - 2);
    if (!stem.isEmpty()) {
      forms << stem + u"FM"_s << stem + u"AM"_s;
    }
  } else {
    forms << base + u"FM"_s << base + u"AM"_s;
  }
  forms << base;
  const QStringList suffixes = {u"-MP3"_s, u"FM-MP3"_s, u"AM-MP3"_s, QString()};

  QList<QUrl> out;
  for (const QString &host : hosts) {
    for (const QString &form : forms) {
      for (const QString &suffix : suffixes) {
        out << QUrl(u"%1/%2%3"_s.arg(host, form, suffix));
      }
    }
  }
  return out;

}

void RadioDiscover::Abort() {

  for (QNetworkReply *reply : replies_) {
    reply->abort();
  }
  replies_.clear();
  for (QProcess *process : processes_) {
    if (process->state() != QProcess::NotRunning) process->kill();
  }
  processes_.clear();

}

void RadioDiscover::Emit(const std::function<void (const QUrl &)> &done, const QUrl &url) {
  Abort();
  done(url);
}

void RadioDiscover::Discover(const QString &slug, const std::function<void (const QUrl &)> &done) {

  // Results are cached so a station is only resolved once per session. The
  // failures are cached too, so an unresolvable station is not re-probed for
  // every search.
  if (cache_.contains(slug)) {
    done(cache_.value(slug));
    return;
  }

  // A directory URL carries the slug but not always a usable call sign, so the
  // slug itself is the only reliable starting point.
  const QString callsign = slug;

  auto finish = [this, slug, done](const QUrl &url) {
    cache_.insert(slug, url);
    done(url);
  };

  QList<QUrl> candidates = StreamtheworldCandidates(callsign);
  if (candidates.isEmpty()) {
    finish(QUrl());
    return;
  }

  pending_level_ = 0;
  // Remembered so the leanstream tier can be reached after the streamtheworld
  // tier is exhausted, without re-deriving the call sign from a URL.
  pending_callsign_ = callsign;
  TryTier(candidates, 0, QUrl(), finish);

}

void RadioDiscover::TryTier(const QList<QUrl> &candidates, int index, const QUrl &slug_url,
                            const std::function<void (const QUrl &)> &done) {

  // A PLS has to be fetched before it yields a media URL, so those candidates
  // are handled separately from direct stream URLs.
  if (index >= candidates.size()) {
    // Tier exhausted. Fall through to leanstream siblings before giving up.
    if (pending_level_ == 0) {
      pending_level_ = 1;
      const QList<QUrl> next = LeanstreamCandidates(pending_callsign_);
      if (!next.isEmpty()) {
        TryTier(next, 0, slug_url, done);
        return;
      }
    }
    done(QUrl());
    return;
  }

  const QUrl candidate = candidates.at(index);
  if (candidate.path().endsWith(u".pls"_s)) {
    FetchThenVerify(candidate, 0,
                    [this, candidates, index, slug_url, done](const QUrl &found) {
      if (found.isValid() && !found.isEmpty()) {
        done(found);
        return;
      }
      // A PLS that yields nothing must not end the search: the next call sign
      // variant may still be served. Without this the tier stopped at the first
      // dead spelling, so only CHOM resolved.
      TryTier(candidates, index + 1, slug_url, done);
    });
    return;
  }
  DecodeTest(candidate, [this, candidate, candidates, index, slug_url, done](bool ok) {
    if (ok) {
      done(candidate);
      return;
    }
    TryTier(candidates, index + 1, slug_url, done);
  });

}

void RadioDiscover::FetchThenVerify(const QUrl &pls_url, int depth,
                                   const std::function<void (const QUrl &)> &done) {

  // Guard against a PLS that lists itself, which would recurse.
  if (depth > 4) {
    done(QUrl());
    return;
  }

  QNetworkRequest request(pls_url);
  request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
  QNetworkReply *reply = network_->get(request);
  replies_.append(reply);

  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, pls_url, depth, done]() {
    replies_.removeAll(reply);
    if (reply->error() != QNetworkReply::NoError) {
      reply->deleteLater();
      done(QUrl());
      return;
    }
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const QList<QString> entries = ExtractPlsEntries(data);
    if (entries.isEmpty()) {
      done(QUrl());
      return;
    }

    // Try each mirror in turn; the first that decodes wins. The type has to be
    // spelled out because the lambda refers to itself.
    std::function<void (int)> try_next;
    try_next = [this, entries, &try_next, done](int i) {
      if (i >= entries.size()) {
        done(QUrl());
        return;
      }
      const QUrl mirror(entries.at(i));
      DecodeTest(mirror, [this, mirror, i, &try_next, done](bool ok) {
          if (ok) {
          done(mirror);
          return;
        }
        // Advance, otherwise this would retry the same dead mirror forever.
        try_next(i + 1);
      });
    };
    try_next(0);

  });

}

void RadioDiscover::DecodeTest(const QUrl &candidate, const std::function<void (bool)> &done) {

  QProcess *process = new QProcess(this);
  process->setProcessChannelMode(QProcess::MergedChannels);
  processes_.append(process);

  // The probe can be concluded by three different events: the timer elapsing,
  // the process exiting on its own, or a spawn failure. Whichever happens
  // first wins. Without this guard the completion callback runs more than once
  // and destroys the process while a lambda still refers to it.
  auto settled = std::make_shared<bool>(false);

  // The verdict has to be read from the pipeline's own diagnostics before the
  // process is stopped. Killing first discards the buffered output, which made
  // every probe report failure regardless of whether the stream played.
  auto read_verdict = [process]() {
    const QString output = QString::fromUtf8(process->readAll());
    if (output.contains(QLatin1String("ERROR"), Qt::CaseInsensitive) ||
        output.contains(QLatin1String("Internal data stream error")) ||
        output.contains(QLatin1String("Could not update any variant"))) {
      return false;
    }
    // The positive signal is that the pipeline began rendering audio. gst-launch
    // reports it either as a bare "PLAYING" line or, while a stream is still
    // buffering, inside "setting pipeline to PLAYING". Matching only the bare
    // line missed every station that buffers on the first read, which is most
    // of them.
    return output.contains(QLatin1String("PLAYING"));
  };

  auto finish = [this, process, done, settled](bool ok) {
    if (*settled) return;
    *settled = true;

    // A live stream never ends, so the pipeline is still running here. It has
    // to be stopped before the object is destroyed, otherwise Qt tears down a
    // QProcess that is still active and the stack is smashed.
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(1500);
    }

    processes_.removeAll(process);
    process->deleteLater();
    done(ok);
  };

  QObject::connect(process, &QProcess::errorOccurred, this,
                   [finish](QProcess::ProcessError) { finish(false); });

  process->start(u"gst-launch-1.0"_s,
                 {u"playbin"_s, u"uri=%1"_s.arg(candidate.toString()), u"audio-sink=fakesink"_s});

  if (!process->waitForStarted(4000)) {
    // Without a decoder no claim can be made, so report failure rather than
    // assuming the stream is fine.
    finish(false);
    return;
  }

  QObject::connect(process, &QProcess::finished, this,
                   [process, finish, read_verdict](int, QProcess::ExitStatus) {
    finish(read_verdict());
  });

  // A live stream does not end, so the probe is deliberately cut short. The
  // verdict is read first, because stopping the pipeline discards whatever is
  // still buffered.
  QTimer::singleShot(kDecodeProbeMs, process, [process, finish, read_verdict]() {
    finish(read_verdict());
  });

}
