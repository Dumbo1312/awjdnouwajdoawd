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
#include <QUuid>
#include <QVector>

#include "../basetypes.h"
#include "../ffs.h"
#include "../ffsengine.h"

// Outcome of a single patch entry after a run
#define PATCH_STATUS_NOT_RUN  0
#define PATCH_STATUS_APPLIED  1
#define PATCH_STATUS_MISSED   2
#define PATCH_STATUS_SKIPPED  3

// One "FileGuid SectionType Patch [Patch...]" line of a patch file, or the
// single line given on the command line with -p
struct PatchEntry {
    PatchEntry();
    QByteArray guid;             // raw 16 byte EFI_GUID
    QString guidText;            // GUID as it was written, for reports
    UINT8 sectionType;
    QVector<PatchData> patches;
    QString setId;               // #!set block this entry belongs to
    QString label;               // nearest preceding plain comment line
    UINT32 lineNumber;
    UINT8 status;
    UINT32 sectionsPatched;
};

// A #!set block: a named group of entries that can be selected or excluded
// as a unit. Set directives are ordinary comments to any older UEFIPatch.
struct PatchSet {
    PatchSet();
    QString id;
    QString description;
    QString risk;                // one word: low, medium, high
    QString note;                // longer caveat, printed under the set
    QVector<int> entries;        // indexes into the entry vector
};

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

    // Prints one line per set found by the last successful load.
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
    UINT8 parsePatchLine(const QByteArray & line, UINT32 lineNumber,
                         const QString & setId, const QString & label,
                         PatchEntry & entry);
    UINT8 parsePatchSpec(const QByteArray & spec, UINT32 lineNumber, PatchData & patch);
    bool isSelected(const PatchEntry & entry, const PatchOptions & options) const;
    UINT8 applyEntry(PatchEntry & entry, const PatchOptions & options);
    UINT8 patchSections(const QModelIndex & index, const QByteArray & fileGuid,
                        const UINT8 sectionType, const QVector<PatchData> & patches,
                        bool allCopies, UINT32 & sectionsPatched);
    void printReport(const PatchOptions & options) const;
    PatchSet & currentSet(const QString & id);

    FfsEngine* ffsEngine;
    TreeModel* model;
    QVector<PatchEntry> entries;
    QVector<PatchSet> sets;
    QStringList warnings;
    QString errorText;
    QString sourceName;
};

#endif
