/* patchset.h

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __PATCHSET_H__
#define __PATCHSET_H__

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "basetypes.h"
#include "ffs.h"
#include "ffsengine.h"

// Outcome of a single patch entry after an attempted run
#define PATCH_STATUS_NOT_RUN  0
#define PATCH_STATUS_APPLIED  1
#define PATCH_STATUS_MISSED   2
#define PATCH_STATUS_SKIPPED  3

// Platform a set is written for, when it does not say
#define PATCH_PLATFORM_ANY "Any"

// One "FileGuid SectionType Patch [Patch...]" line of a patch file, or the
// single line given on the command line with -p
struct PatchEntry {
    PatchEntry();
    QByteArray guid;             // raw 16 byte EFI_GUID
    QString guidText;            // GUID as it was written, for reports
    UINT8 sectionType;
    QVector<PatchData> patches;
    QString setId;               // #!set block this entry belongs to
    QString label;               // nearest preceding plain comment
    UINT32 lineNumber;
    UINT8 status;
    UINT32 sectionsPatched;
};

// A #!set block: a named group of entries that can be selected as a unit,
// both by UEFIPatch's --set and by the preset list in the GUI. Set directives
// all start with '#', so any older UEFIPatch reads them as comments.
struct PatchSet {
    PatchSet();
    bool isApplicable() const;   // has patch lines, so there is something to do

    QString id;
    QString description;
    QString risk;                // one word: low, medium, high
    QString note;                // longer caveat
    QString platform;            // Intel, AMD, GPU, ... for grouping presets
    QString info;                // set to explain a deliberately empty set
    QVector<int> entries;        // indexes into PatchCatalog::entries()
};

// Reads and validates a patch file, without touching any image.
class PatchCatalog
{
public:
    PatchCatalog();

    // Reads a whole patch file. Safe to call just to check a file is sane.
    UINT8 loadFile(const QString & path);
    // Parses a single "FileGuid SectionType Patch" specification.
    UINT8 loadLine(const QString & patch);

    const QVector<PatchEntry> & entries() const { return patchEntries; }
    QVector<PatchEntry> & entries() { return patchEntries; }
    const QVector<PatchSet> & sets() const { return patchSets; }

    // Platform names in the order the file introduced them.
    QStringList platforms() const;
    // The set with this id, or NULL.
    const PatchSet* setById(const QString & id) const;

    // Puts every entry back to PATCH_STATUS_NOT_RUN, so a catalog can be
    // applied more than once without reloading it.
    void resetStatus();

    QString error() const { return errorText; }
    QStringList warnings() const { return warningList; }
    QString sourceName() const { return source; }

private:
    UINT8 parseLine(const QByteArray & line, UINT32 lineNumber,
                    const QString & setId, const QString & label,
                    PatchEntry & entry);
    UINT8 parseSpec(const QByteArray & spec, UINT32 lineNumber, PatchData & patch);
    PatchSet & currentSet(const QString & id);
    void clear(const QString & newSource);

    QVector<PatchEntry> patchEntries;
    QVector<PatchSet> patchSets;
    QStringList warningList;
    QString errorText;
    QString source;
};

// Applies one entry to the image held by engine/model, counting the sections
// it changed. With allCopies false it stops at the first matching section,
// which is what UEFIPatch has always done.
UINT8 applyPatchEntry(FfsEngine* engine, TreeModel* model, PatchEntry & entry, bool allCopies);

#endif
