/*
 * Mango Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2013-2026, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Mango is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mango is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mango.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QCoreApplication>
#include <QWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QString>
#include <QFlags>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QKeySequence>
#include <QTextBrowser>

#include "aboutdialog.h"
#include "ui_aboutdialog.h"

using namespace Qt::Literals::StringLiterals;

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent), ui_{} {

  ui_.setupUi(this);
  setWindowFlags(windowFlags()|Qt::WindowStaysOnTopHint);
  setWindowTitle(tr("About Mango"));

  strawberry_authors_ \
           << Person(u"Jonas Kvinge"_s);

  strawberry_contributors_ \
           << Person(u"Gavin D. Howard"_s)
           << Person(u"Martin Delille"_s)
           << Person(u"Roman Lebedev"_s)
           << Person(u"Daniel Ostertag"_s)
           << Person(u"Gustavo L Conte"_s)
           << Person(u"Adam Hill"_s)
           << Person(u"Alexey Sokolov"_s)
           << Person(u"Alexey Vazhnov"_s)
           << Person(u"Andrei Stepanov"_s)
           << Person(u"Andrew Tribick"_s)
           << Person(u"Benji Hartman"_s)
           << Person(u"Célestin Matte"_s)
           << Person(u"Cesar Enrique Garcia Dabo"_s)
           << Person(u"Chongo Bong"_s)
           << Person(u"Christian Kr"_s)
           << Person(u"Claudiu Mn"_s)
           << Person(u"Daniel Kolesa"_s)
           << Person(u"Edgar Salgado"_s)
           << Person(u"Eoin O'Neill"_s)
           << Person(u"Felipe Bugno"_s)
           << Person(u"Fletcher Dostie"_s)
           << Person(u"Gaganpreet Arora"_s)
           << Person(u"Gregor Santner"_s)
           << Person(u"Ike Devolder"_s)
           << Person(u"Jacob Henner"_s)
           << Person(u"Jiří Pinkava"_s)
           << Person(u"Kientz Arnaud"_s)
           << Person(u"Kyle Hopkins"_s)
           << Person(u"Lars Wendler"_s)
           << Person(u"Leandro Matheus"_s)
           << Person(u"Madeline Schreiber"_s)
           << Person(u"Malte Zilinski"_s)
           << Person(u"Marcus Müller"_s)
           << Person(u"Matteo Lo Potro"_s)
           << Person(u"Maxime Haselbauer"_s)
           << Person(u"Michał Walenciak"_s)
           << Person(u"Mikalai Daronin"_s)
           << Person(u"Mikel Pérez"_s)
           << Person(u"Nicholas Bissell"_s)
           << Person(u"Nicolas Toussaint"_s)
           << Person(u"Octavio Calleya Garcia"_s)
           << Person(u"Olivier Humbert"_s)
           << Person(u"Ondrej Mosnáček"_s)
           << Person(u"Pascal Below"_s)
           << Person(u"Piper McCorkle"_s)
           << Person(u"Robert Gingras"_s)
           << Person(u"Robert Marshall"_s)
           << Person(u"Rob Stanfield"_s)
           << Person(u"Sami Boukortt"_s)
           << Person(u"Sebastian Thomas"_s)
           << Person(u"Sungrak Choi"_s)
           << Person(u"Tom Kranz"_s)
           << Person(u"William Andrea"_s)
           << Person(u"Yaroslav Chvanov"_s)
           << Person(u"Alex Bikadorov"_s);

  clementine_authors_
           << Person(u"David Sansome"_s)
           << Person(u"John Maguire"_s)
           << Person(u"Paweł Bara"_s)
           << Person(u"Arnaud Bienner"_s);

  clementine_contributors_ \
           << Person(u"Jakub Stachowski"_s)
           << Person(u"Paul Cifarelli"_s)
           << Person(u"Felipe Rivera"_s)
           << Person(u"Alexander Peitz"_s)
           << Person(u"Andreas Muttscheller"_s)
           << Person(u"Mark Furneaux"_s)
           << Person(u"Florian Bigard"_s)
           << Person(u"Mattias Andersson"_s)
           << Person(u"Alan Briolat"_s)
           << Person(u"Arun Narayanankutty"_s)
           << Person(u"Bartłomiej Burdukiewicz"_s)
           << Person(u"Andre Siviero"_s)
           << Person(u"Santiago Gil"_s)
           << Person(u"Tyler Rhodes"_s)
           << Person(u"Vikram Ambrose"_s)
           << Person(u"David Guillen"_s)
           << Person(u"Krzysztof Sobiecki"_s)
           << Person(u"Valeriy Malov"_s)
           << Person(u"Nick Lanham"_s);

  strawberry_thanks_ \
           << Person(u"Mark Kretschmann"_s)
           << Person(u"Max Howell"_s)
           << Person(u"Artur Rona"_s)
           << Person(u"Robert-André Mauchin"_s)
           << Person(u"Thomas Pierson"_s)
           << Person(u"Fabio Loli"_s);

  QFont title_font;
  title_font.setBold(true);
  title_font.setPointSize(title_font.pointSize() + 4);

  ui_.label_title->setFont(title_font);
  ui_.label_title->setText(windowTitle());
  ui_.label_text->setText(MainHtml());
  ui_.text_contributors->document()->setDefaultStyleSheet(QStringLiteral("a {color: %1; }").arg(palette().text().color().name()));
  ui_.text_contributors->setText(ContributorsHtml());

  ui_.buttonBox->button(QDialogButtonBox::Close)->setShortcut(QKeySequence::Close);

}

QString AboutDialog::MainHtml() const {

  QString ret;

  const QString colour = palette().text().color().name();
  const QString link = QStringLiteral("<a style=\"color:%1;\">").arg(colour);

  ret += "<p>"_L1;
  ret += tr("Version %1").arg(QCoreApplication::applicationVersion());
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += tr("Mango Music Player is a music player and collection organizer.");
  ret += "<br />"_L1;
  ret += tr("It plays your own collection, streams internet radio, and reaches "
            "streaming services. It is aimed at music collectors and audiophiles, "
            "and it is built to keep working as those sources change.");
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += tr("Mango repairs what it can rather than hiding it. When a stream "
            "directory returns a playlist that cannot play, the failure is "
            "detected and the station is resolved again, so adding a station "
            "keeps working instead of breaking.");
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += tr("Mango is free software under the GNU General Public License, "
            "version 3 or later. The source code is on %1%2%3%4.")
      .arg(link,
           QStringLiteral("https://github.com/BeRightBack/mango"),
           QStringLiteral(">GitHub</a>"),
           QStringLiteral("."));
  ret += "<br />"_L1;
  ret += tr("It is a custom port of Strawberry Music Player 1.2.30, which is in "
            "turn a fork of Clementine. The upstream projects and their authors "
            "are credited in full under %1%2%3%4. All upstream copyright notices "
            "are retained, as the licence requires.")
      .arg(link,
           QStringLiteral("https://www.strawberrymusicplayer.org/"),
           QStringLiteral(">their websites</a>"),
           QStringLiteral("."));
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += tr("Released free of charge and not for sale.");
  ret += "</p>"_L1;

  return ret;

}

QString AboutDialog::ContributorsHtml() const {

  QString ret;

  ret += "<p>"_L1;
  ret += "<b>"_L1;
  ret += tr("Author and maintainer");
  ret += "</b>"_L1;
  for (const Person &person : strawberry_authors_) {
    ret += "<br />"_L1 + PersonToHtml(person);
  }
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += "<b>"_L1;
  ret += tr("Contributors");
  ret += "</b>"_L1;
  for (const Person &person : strawberry_contributors_) {
    ret += "<br />"_L1 + PersonToHtml(person);
  }
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += "<b>"_L1;
  ret += tr("Clementine authors");
  ret += "</b>"_L1;
  for (const Person &person : clementine_authors_) {
    ret += "<br />"_L1 + PersonToHtml(person);
  }
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += "<b>"_L1;
  ret += tr("Clementine contributors");
  ret += "</b>"_L1;
  for (const Person &person : clementine_contributors_) {
    ret += "<br />"_L1 + PersonToHtml(person);
  }
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += "<b>"_L1;
  ret += tr("Thanks to");
  ret += "</b>"_L1;
  for (const Person &person : strawberry_thanks_) {
    ret += "<br />"_L1 + PersonToHtml(person);
  }
  ret += "</p>"_L1;

  ret += "<p>"_L1;
  ret += tr("Thanks to all the other Amarok and Clementine contributors.");
  ret += "</p>"_L1;
  return ret;

}

QString AboutDialog::PersonToHtml(const Person &person) {

  if (person.email.isEmpty()) {
    return person.name;
  }

  return QStringLiteral("%1 &lt;<a href=\"mailto:%2\">%3</a>&gt;").arg(person.name, person.email, person.email);

}
