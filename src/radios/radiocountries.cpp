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

#include "radiocountries.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QSet>

#include "constants/radiobrowsersettings.h"

using namespace Qt::Literals::StringLiterals;

QStringList RadioCountries::Default() {

  // English and French Canada, where the stations that need repairing are, plus
  // the neighbours whose radio is most likely to appear in a Canadian playlist.
  // A fresh install searches these rather than every country, because an
  // unfiltered search returns stations nobody asked for and makes discovery
  // impractical.
  return {u"CA"_s, u"US"_s, u"GB"_s, u"IE"_s};

}

QStringList RadioCountries::Read() {

  QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
  const QVariant stored = s.value(QLatin1String(RadioBrowserSettings::kEnabledCountries));
  if (!stored.isValid()) return Default();

  const QString raw = stored.toString();
  if (raw.trimmed().isEmpty()) return {};

  QStringList codes;
  for (const QString &part : raw.split(u',', Qt::SkipEmptyParts)) {
    const QString code = part.trimmed().toUpper();
    // Anything that is not a two letter code is discarded rather than sent to
    // the directory, which would silently widen the filter.
    if (code.length() == 2) codes << code;
  }
  codes.removeDuplicates();
  return codes;

}

void RadioCountries::Write(const QStringList &codes) {

  QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
  s.setValue(QLatin1String(RadioBrowserSettings::kEnabledCountries), codes.join(u','));
  s.sync();

}

QStringList RadioCountries::Enabled() {
  return Read();
}

void RadioCountries::SetEnabled(const QStringList &codes) {

  QStringList cleaned;
  for (const QString &part : codes) {
    const QString code = part.trimmed().toUpper();
    if (code.length() == 2) cleaned << code;
  }
  cleaned.removeDuplicates();
  Write(cleaned);

}

bool RadioCountries::IsEnabled(const QString &country_code) {

  const QStringList codes = Read();
  // An explicitly empty selection means every country, which is what the
  // "Select none" button is for.
  if (codes.isEmpty()) return true;

  // Many directory entries carry no country at all. Dropping them made a search
  // look empty apart from the handful of stations that happen to be tagged, so
  // an untagged station is kept rather than silently excluded. It can still be
  // added and played; it is simply not filtered out on a guess.
  if (country_code.trimmed().isEmpty()) return true;

  return codes.contains(country_code.toUpper());

}

QString RadioCountries::RequestedCountryCode(const QString &fallback) {

  const QStringList codes = Read();
  // The directory accepts one countrycode per request, so a single enabled
  // country is pushed down to the server and the result set narrowed before
  // anything is fetched. Several countries have to be filtered here instead.
  if (codes.size() == 1) return codes.first();
  return fallback;

}

bool RadioCountries::DiscoveryAllowed() {

  // A decode probe is spent per candidate, so discovery only pays off when the
  // search has been narrowed to countries whose stations are wanted. The
  // default set is narrow enough for that.
  return !Read().isEmpty();

}

QList<QPair<QString, QString>> RadioCountries::All() {

  QSet<QLocale::Territory> seen;
  QList<QPair<QString, QString>> countries;

  const QList<QLocale> locales = QLocale::matchingLocales(
      QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
  for (const QLocale &locale : locales) {
    const QLocale::Territory territory = locale.territory();
    if (territory == QLocale::AnyTerritory || seen.contains(territory)) continue;
    seen.insert(territory);

    const QString locale_name = locale.name();
    const int underscore = static_cast<int>(locale_name.lastIndexOf(u'_'));
    if (underscore < 0) continue;
    const QString code = locale_name.mid(underscore + 1);
    if (code.length() != 2) continue;

    countries << qMakePair(QLocale::territoryToString(territory), code);
  }

  std::sort(countries.begin(), countries.end(),
            [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
    return a.first.localeAwareCompare(b.first) < 0;
  });
  return countries;

}