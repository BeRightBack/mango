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

#ifndef RADIOCOUNTRIES_H
#define RADIOCOUNTRIES_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// The countries radio search and stream discovery are allowed to consider.
//
// Radio Browser serves stations from every country, so an unfiltered search
// returns large numbers of stations that will never be listened to. That is
// not merely noise: stream discovery runs a decode probe per candidate, so an
// unfiltered search turns into a long chain of probes for stations nobody
// wanted.
//
// The selection is stored as a comma separated list of ISO 3166 alpha-2 codes
// under RadioBrowser/enabled_countries. An empty list means every country,
// which preserves the previous behaviour for anyone who has not chosen one.
class RadioCountries {

 public:
  // The countries the user has enabled, upper case two letter codes. When
  // nothing has ever been chosen the default set is used, so a fresh install
  // searches a handful of countries rather than the whole world.
  static QStringList Enabled();

  static void SetEnabled(const QStringList &codes);

  // True when the given country code passes the current selection.
  static bool IsEnabled(const QString &country_code);

  // The country code to send in a directory request. Returns the single
  // enabled country when exactly one is selected, since the directory takes a
  // single countrycode rather than a list.
  static QString RequestedCountryCode(const QString &fallback);

  // Whether discovery should be attempted at all. Discovery only pays off for
  // a narrow selection, and a station outside the selection is not wanted
  // regardless of whether a stream could be found for it.
  static bool DiscoveryAllowed();

  // Every country known to Qt, as (display name, ISO code) pairs, sorted by
  // display name.
  static QList<QPair<QString, QString>> All();

  // Countries enabled on a fresh install, when the user has not chosen any.
  static QStringList Default();

 private:
  static QStringList Read();
  static void Write(const QStringList &codes);

};

#endif  // RADIOCOUNTRIES_H