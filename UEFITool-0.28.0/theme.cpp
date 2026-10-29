/* theme.cpp

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QApplication>
#include <QFile>

#include "theme.h"

#define THEME_RESOURCE ":/theme/dark-yellow.qss"

namespace Theme
{

QString accentColor()
{
    return QString("#ffc400");
}

QString textColor()
{
    return QString("#e6e4dc");
}

QString dimTextColor()
{
    return QString("#9a978c");
}

QString panelColor()
{
    return QString("#17171a");
}

QString styleSheet()
{
    QFile file(THEME_RESOURCE);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();

    const QString sheet = QString::fromUtf8(file.readAll());
    file.close();
    return sheet;
}

void apply(bool enabled)
{
    if (!qApp)
        return;

    qApp->setStyleSheet(enabled ? styleSheet() : QString());
}

}
