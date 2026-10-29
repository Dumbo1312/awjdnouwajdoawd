/* theme.h

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __THEME_H__
#define __THEME_H__

#include <QString>

// The yellow on black theme, applied application wide. Keeping it in one
// place means the preset dialog and the main window can not drift apart, and
// turning it off restores whatever style the platform would have used.
namespace Theme
{
    // Accent colours, for the few places that need them in code rather than
    // in the stylesheet, such as per item foregrounds in a tree widget.
    QString accentColor();       // yellow, for text on the dark background
    QString warningColor();      // orange, for findings that need attention
    QString textColor();
    QString dimTextColor();
    QString panelColor();

    // Window translucency, as a percentage. 100 is opaque. Only has a visible
    // effect where the desktop has a compositor running.
    int minimumOpacity();
    int defaultOpacity();

    // Reads the stylesheet out of the compiled in resource. Empty if the
    // resource is missing, which is not fatal: the application just looks
    // like the platform default.
    QString styleSheet();

    // Applies or removes the theme on the running QApplication.
    void apply(bool enabled);
}

#endif
