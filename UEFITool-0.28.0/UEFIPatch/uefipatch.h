/* uefipatch.h

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __UEFIPATCH_H__
#define __UEFIPATCH_H__

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QFileInfo>

#include "../basetypes.h"
#include "../ffs.h"
#include "../ffsengine.h"
#include "../patchset.h"

// Run time options that are not paths
struct PatchOptions {
    PatchOptions();
    bool verbose;                // report every entry, not just the summary
    bool dryRun;                 // do all the work, write nothing
    bool strict;                 // fail if any selected entry did not apply
    bool allCopies;              // patch every matching section, not just the first
    bool force;                  // allow writing the output over the input
    bool hashes;                 // print input and output digests
    QStringList includeSets;     // only these sets, if not empty
    QStringList excludeSets;     // never these sets
};

class UEFIPatch : public QObject
{
    Q_OBJECT

public:
    explicit UEFIPatch(QObject *parent = 0);
    ~UEFIPatch();

    // Reads and fully validates a patch file. Touches no image, so it is safe
    // to call just to find out whether a patch file is well formed.
    UINT8 loadPatchFile(const QString & path);
    // Parses a single "FileGuid SectionType Patch" command line argument.
    UINT8 loadPatchArgument(const QString & patch);

    // Prints the sets found by the last successful load.
    void listSets() const;

    // Applies the loaded patches to path, writing the result to outputPath.
    UINT8 run(const QString & path, const QString & outputPath, const PatchOptions & options);

    // Human readable detail for the last error, empty if there is none.
    QString lastError() const;
    // Non fatal complaints collected while loading, such as a replacement
    // pattern that is not the same length as the pattern it replaces.
    QStringList lastWarnings() const;

    // Kept so that existing callers and scripts keep working unchanged.
    UINT8 patchFromFile(const QString & path, const QString & patches, const QString & outputPath);
    UINT8 patchFromArg(const QString & path, const QString & patch, const QString & outputPath);

private:
    bool isSelected(const PatchEntry & entry, const PatchOptions & options) const;
    void printReport(const PatchOptions & options) const;

    FfsEngine* ffsEngine;
    TreeModel* model;
    PatchCatalog catalog;
    QString errorText;           // errors raised by run(), not by loading
};

#endif
