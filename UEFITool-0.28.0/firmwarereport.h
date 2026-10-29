/* firmwarereport.h

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __FIRMWAREREPORT_H__
#define __FIRMWAREREPORT_H__

#include <QByteArray>
#include <QMap>
#include <QPersistentModelIndex>
#include <QString>
#include <QStringList>
#include <QVector>

#include "basetypes.h"
#include "ffs.h"
#include "ffsengine.h"
#include "patchset.h"

// Which silicon the image is built for
#define FW_PLATFORM_UNKNOWN 0
#define FW_PLATFORM_INTEL   1
#define FW_PLATFORM_AMD     2
#define FW_PLATFORM_BOTH    3   // evidence for both, which means the guess is unreliable

// How confident the dialogs should sound about a preset or a candidate
#define FW_MATCH_ABSENT     0   // the module is not in this image at all
#define FW_MATCH_PRESENT    1   // the module is here but the pattern does not match
#define FW_MATCH_APPLIES    2   // the pattern matches, applying will change something

// One module (an FFS file) found in the opened image
struct ModuleInfo {
    ModuleInfo();
    QString guid;                   // file GUID, as the model prints it
    QByteArray guidBytes;           // the same GUID, raw, for cross referencing
    QString name;                   // the module's own name from its UI section
    QString volume;                 // which volume it lives in, for display
    UINT8 fileType;
    UINT32 size;
    QPersistentModelIndex index;    // survives the model changing under us
};

// A line of knowledge/debloat.txt
struct DebloatEntry {
    DebloatEntry();
    bool byGuid;                    // false means match on the module's name
    QString match;
    QString description;
    QString categoryId;
    UINT32 lineNumber;
};

// A #!category block of knowledge/debloat.txt
struct DebloatCategory {
    DebloatCategory();
    QString id;
    QString label;
    QString risk;                   // low, medium, high
    QString platform;               // Intel, AMD or Any
    QStringList impact;             // what removing this actually gains
    QStringList notes;
    QStringList requirements;       // things the user must have checked first
    bool isProtected;               // listed so you can see it, never removable
    QVector<int> entries;           // indexes into DebloatCatalog::entries()
};

// A catalogue entry that matched a module actually present in the image,
// together with everything the tool could work out about removing it.
struct DebloatCandidate {
    DebloatCandidate();
    ModuleInfo module;
    QString categoryId;
    QString description;

    // Dependency findings. None of these prove a removal is safe; they only
    // prove it is unsafe, which is the useful direction.
    QStringList referencedBy;       // modules whose body contains this GUID
    bool inApriori;                 // listed in a PEI or DXE apriori file
    QString aprioriName;
    bool isLastOfKind;              // the only module of its type in the image

    bool looksRisky() const;        // any finding at all
};

// Reads knowledge/debloat.txt
class DebloatCatalog
{
public:
    DebloatCatalog();

    UINT8 loadFile(const QString & path);
    static QString defaultPath();

    const QVector<DebloatEntry> & entries() const { return catalogEntries; }
    const QVector<DebloatCategory> & categories() const { return catalogCategories; }
    const DebloatCategory* categoryById(const QString & id) const;

    QString error() const { return errorText; }
    QStringList warnings() const { return warningList; }
    QString sourceName() const { return source; }

private:
    DebloatCategory & currentCategory(const QString & id);

    QVector<DebloatEntry> catalogEntries;
    QVector<DebloatCategory> catalogCategories;
    QStringList warningList;
    QString errorText;
    QString source;
};

// What a scan of the opened image found
struct FirmwareReport {
    FirmwareReport();

    UINT8 platform;
    QString platformName;
    QStringList platformEvidence;   // why the scanner thinks so

    QString vendor;                 // AMI Aptio, Insyde, Phoenix, ...
    QStringList vendorEvidence;

    QStringList regions;            // descriptor, ME, GbE, BIOS, ...
    bool hasDescriptor;
    bool hasMeRegion;

    QVector<ModuleInfo> modules;
    UINT32 volumeCount;
    UINT32 namedModuleCount;
    quint64 totalModuleBytes;

    QStringList aprioriGuids;       // every GUID listed in an apriori file

    // A short human readable summary, for the Messages list and the dialogs.
    QStringList summaryLines() const;
};

// Walks a parsed image and works out what it is looking at.
class FirmwareScanner
{
public:
    FirmwareScanner(FfsEngine* engine, TreeModel* model);

    // Inventory, platform and vendor detection. Cheap enough to run on open.
    FirmwareReport scan() const;

    // For each set in the catalogue, the best outcome any of its lines can
    // reach against this image. Read only: nothing is patched.
    QMap<QString, UINT8> evaluatePresets(const PatchCatalog & catalog) const;
    // Per line, so a dialog can say which of them will do something.
    QMap<int, UINT8> evaluateEntries(const PatchCatalog & catalog) const;

    // Catalogue entries matched against the modules in the report, with the
    // dependency analysis filled in.
    QVector<DebloatCandidate> findCandidates(const DebloatCatalog & catalog,
                                             const FirmwareReport & report) const;

private:
    void walk(const QModelIndex & index, FirmwareReport & report, const QString & volume) const;
    QString uiNameOf(const QModelIndex & fileIndex) const;
    void detectPlatform(FirmwareReport & report) const;
    void detectVendor(FirmwareReport & report) const;
    void collectApriori(FirmwareReport & report) const;
    UINT8 evaluateEntry(const PatchEntry & entry) const;

    FfsEngine* ffsEngine;
    TreeModel* model;
};

#endif
