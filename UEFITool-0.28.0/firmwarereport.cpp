/* firmwarereport.cpp

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegExp>
#include <QSet>

#include "firmwarereport.h"
#include "types.h"

#define DEBLOAT_FILE_NAME "debloat.txt"

// How many referencing modules to name before saying "and others"
#define MAX_REFERENCES_REPORTED 6

ModuleInfo::ModuleInfo() :
    fileType(0), size(0)
{
}

DebloatEntry::DebloatEntry() :
    byGuid(false), lineNumber(0)
{
}

DebloatCategory::DebloatCategory() :
    platform(PATCH_PLATFORM_ANY)
{
}

DebloatCandidate::DebloatCandidate() :
    inApriori(false), isLastOfKind(false)
{
}

bool DebloatCandidate::looksRisky() const
{
    return inApriori || !referencedBy.isEmpty();
}

FirmwareReport::FirmwareReport() :
    platform(FW_PLATFORM_UNKNOWN), hasDescriptor(false), hasMeRegion(false),
    volumeCount(0), namedModuleCount(0), totalModuleBytes(0)
{
}

// --------------------------------------------------------------- helpers ---

static QList<QByteArray> splitFields(const QByteArray & line)
{
    QByteArray normalized = line;
    normalized.replace('\t', ' ');

    QList<QByteArray> fields;
    const QList<QByteArray> raw = normalized.split(' ');
    for (int i = 0; i < raw.count(); i++) {
        const QByteArray field = raw.at(i).trimmed();
        if (!field.isEmpty())
            fields.append(field);
    }
    return fields;
}

// Does hexFindPattern occur in body, on a byte boundary? The same matching
// FfsEngine::patchViaPattern does, without changing anything, so a dialog can
// say whether a patch would do something before anyone applies it.
static bool patternOccursIn(const QByteArray & body, const QByteArray & hexFindPattern)
{
    if (body.isEmpty() || hexFindPattern.isEmpty() || hexFindPattern.length() % 2 != 0)
        return false;

    const QString hexBody = QString(body.toHex());
    QRegExp regexp = QRegExp(QString(hexFindPattern), Qt::CaseInsensitive);

    int offset = regexp.indexIn(hexBody);
    while (offset >= 0) {
        if (offset % 2 == 0)
            return true;
        offset = regexp.indexIn(hexBody, offset + 1);
    }
    return false;
}

// Every leaf section body of a file, joined. Used for cross referencing and
// for signature scanning.
static QByteArray fileBlob(TreeModel* model, const QModelIndex & fileIndex)
{
    QByteArray blob;
    for (int i = 0; i < model->rowCount(fileIndex); i++) {
        const QModelIndex child = model->index(i, 0, fileIndex);
        if (model->rowCount(child) == 0)
            blob.append(model->body(child));
        else
            blob.append(fileBlob(model, child));
    }
    return blob;
}

static void collectFiles(TreeModel* model, const QModelIndex & index, QModelIndexList & files)
{
    if (!index.isValid())
        return;
    if (model->type(index) == Types::File)
        files.append(index);
    for (int i = 0; i < model->rowCount(index); i++)
        collectFiles(model, model->index(i, 0, index), files);
}

// ------------------------------------------------------- debloat catalog ---

DebloatCatalog::DebloatCatalog()
{
}

QString DebloatCatalog::defaultPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    candidates << QDir(appDir).absoluteFilePath(DEBLOAT_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("knowledge/" DEBLOAT_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("../knowledge/" DEBLOAT_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("../../knowledge/" DEBLOAT_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("../../../knowledge/" DEBLOAT_FILE_NAME);
    candidates << QString("knowledge/" DEBLOAT_FILE_NAME);
    candidates << QString(DEBLOAT_FILE_NAME);

    for (int i = 0; i < candidates.count(); i++) {
        const QFileInfo info(candidates.at(i));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }
    return QString();
}

const DebloatCategory* DebloatCatalog::categoryById(const QString & id) const
{
    for (int i = 0; i < catalogCategories.count(); i++) {
        if (catalogCategories.at(i).id == id)
            return &catalogCategories.at(i);
    }
    return NULL;
}

DebloatCategory & DebloatCatalog::currentCategory(const QString & id)
{
    for (int i = 0; i < catalogCategories.count(); i++) {
        if (catalogCategories.at(i).id == id)
            return catalogCategories[i];
    }

    DebloatCategory category;
    category.id = id;
    catalogCategories.append(category);
    return catalogCategories[catalogCategories.count() - 1];
}

UINT8 DebloatCatalog::loadFile(const QString & path)
{
    catalogEntries.clear();
    catalogCategories.clear();
    warningList.clear();
    errorText.clear();
    source = path;

    QFile file;
    file.setFileName(path);
    if (!file.exists()) {
        errorText = QObject::tr("%1 does not exist").arg(path);
        return ERR_INVALID_FILE;
    }
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        errorText = QObject::tr("%1 can not be read").arg(path);
        return ERR_INVALID_FILE;
    }

    QString categoryId;
    UINT32 lineNumber = 0;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        lineNumber++;

        if (line.isEmpty())
            continue;

        if (line.startsWith("#!")) {
            const QList<QByteArray> fields = splitFields(line.mid(2));
            if (fields.isEmpty())
                continue;

            const QByteArray directive = fields.at(0).toLower();
            QStringList rest;
            for (int f = 1; f < fields.count(); f++)
                rest.append(QString::fromUtf8(fields.at(f)));
            const QString argument = rest.join(" ");

            if (directive == "category") {
                if (rest.isEmpty()) {
                    errorText = QObject::tr("%1 line %2: #!category needs a name")
                            .arg(path).arg(lineNumber);
                    return ERR_INVALID_PARAMETER;
                }
                categoryId = rest.at(0);
                currentCategory(categoryId);
            }
            else if (categoryId.isEmpty()) {
                warningList.append(QObject::tr("%1 line %2: #!%3 before any #!category, ignored")
                                   .arg(path).arg(lineNumber).arg(QString::fromUtf8(directive)));
            }
            else if (directive == "label") {
                currentCategory(categoryId).label = argument;
            }
            else if (directive == "risk") {
                currentCategory(categoryId).risk = rest.isEmpty() ? QString() : rest.at(0);
            }
            else if (directive == "platform") {
                currentCategory(categoryId).platform = argument;
            }
            else if (directive == "impact") {
                currentCategory(categoryId).impact.append(argument);
            }
            else if (directive == "note") {
                currentCategory(categoryId).notes.append(argument);
            }
            else if (directive == "requires") {
                currentCategory(categoryId).requirements.append(argument);
            }
            else {
                warningList.append(QObject::tr("%1 line %2: unknown directive #!%3, ignored")
                                   .arg(path).arg(lineNumber).arg(QString::fromUtf8(directive)));
            }
            continue;
        }

        if (line.at(0) == '#')
            continue;

        const QList<QByteArray> fields = splitFields(line);
        if (fields.count() < 2) {
            warningList.append(QObject::tr("%1 line %2: expected \"N <name>\" or \"G <guid>\" "
                                           "followed by a description, ignored")
                               .arg(path).arg(lineNumber));
            continue;
        }

        if (categoryId.isEmpty()) {
            warningList.append(QObject::tr("%1 line %2: entry before any #!category, ignored")
                               .arg(path).arg(lineNumber));
            continue;
        }

        const QByteArray kind = fields.at(0).toUpper();
        if (kind != "N" && kind != "G") {
            warningList.append(QObject::tr("%1 line %2: \"%3\" is not N or G, ignored")
                               .arg(path).arg(lineNumber).arg(QString::fromUtf8(fields.at(0))));
            continue;
        }

        DebloatEntry entry;
        entry.byGuid = (kind == "G");
        entry.match = QString::fromUtf8(fields.at(1));
        entry.categoryId = categoryId;
        entry.lineNumber = lineNumber;

        QStringList description;
        for (int f = 2; f < fields.count(); f++)
            description.append(QString::fromUtf8(fields.at(f)));
        entry.description = description.join(" ");

        currentCategory(categoryId).entries.append(catalogEntries.count());
        catalogEntries.append(entry);
    }

    file.close();

    if (catalogEntries.isEmpty()) {
        errorText = QObject::tr("%1 lists nothing that can be removed").arg(path);
        return ERR_INVALID_FILE;
    }

    return ERR_SUCCESS;
}

// -------------------------------------------------------------- scanning ---

FirmwareScanner::FirmwareScanner(FfsEngine* engine, TreeModel* treeModel) :
    ffsEngine(engine), model(treeModel)
{
}

QString FirmwareScanner::uiNameOf(const QModelIndex & fileIndex) const
{
    // parseSection copies a user interface section's string onto the parent
    // file, so the module's own name is already there when there is one.
    const QString text = model->text(fileIndex);
    return text.trimmed();
}

void FirmwareScanner::walk(const QModelIndex & index, FirmwareReport & report,
                           const QString & volume) const
{
    if (!index.isValid())
        return;

    QString currentVolume = volume;

    switch (model->type(index)) {
    case Types::Region: {
        const QString name = model->name(index);
        if (!report.regions.contains(name))
            report.regions.append(name);
        if (model->subtype(index) == Subtypes::DescriptorRegion)
            report.hasDescriptor = true;
        else if (model->subtype(index) == Subtypes::MeRegion)
            report.hasMeRegion = true;
    } break;

    case Types::Volume:
        report.volumeCount++;
        currentVolume = model->name(index);
        break;

    case Types::File: {
        // A module already marked for removal is on its way out; listing it
        // again would let a second pass offer to remove it twice.
        if (model->action(index) == Actions::Remove)
            return;

        ModuleInfo info;
        info.guid = model->name(index);
        info.guidBytes = model->header(index).left(sizeof(EFI_GUID));
        info.name = uiNameOf(index);
        info.volume = currentVolume;
        info.fileType = model->subtype(index);
        info.size = (UINT32)(model->header(index).size() + model->body(index).size());
        info.index = QPersistentModelIndex(index);

        if (!info.name.isEmpty())
            report.namedModuleCount++;
        report.totalModuleBytes += info.size;
        report.modules.append(info);
    } break;

    default:
        break;
    }

    for (int i = 0; i < model->rowCount(index); i++)
        walk(model->index(i, 0, index), report, currentVolume);
}

// Module names that only ever belong to one vendor's platform code. Matching
// on a name the image itself carries beats guessing from GUIDs, which differ
// between vendors and BIOS versions.
static const char* INTEL_MODULE_HINTS[] = {
    "SiInit", "PchInit", "SaInit", "PowerMgmtDxe", "PpmInitialize", "CpuPei",
    "IgdOpRegion", "PchSmiDispatcher", "MeSmm", "MeFwDowngrade", "AmtWrapper",
    "PchSpi", "SaSsdt", "PchResetSystem", "MeUma", NULL
};

static const char* AMD_MODULE_HINTS[] = {
    "AmdCpm", "AmdInit", "AmdPsp", "AmdSoc", "AmdCpuDxe", "AmdNb", "AmdSb",
    "AmdAgesa", "Agesa", "AmdFabric", "AmdMem", "AmdSmbios", "FchInit", NULL
};

// Longer ASCII strings found in module bodies. Each is specific enough that a
// chance hit is not a realistic worry.
struct Signature {
    const char* needle;
    const char* meaning;
    UINT8 platform;          // FW_PLATFORM_* or UNKNOWN for a vendor hint
    const char* vendor;
};

static const Signature SIGNATURES[] = {
    { "AGESA!",                    "AGESA firmware marker",            FW_PLATFORM_AMD,     NULL },
    { "AMD-AGESA",                 "AGESA firmware marker",            FW_PLATFORM_AMD,     NULL },
    { "AmdCpmOemInit",             "AMD CPM OEM init",                 FW_PLATFORM_AMD,     NULL },
    { "$IBIOSI$",                  "Intel BIOS info header",           FW_PLATFORM_INTEL,   NULL },
    { "Intel(R) Management Engine","Intel Management Engine string",    FW_PLATFORM_INTEL,   NULL },
    { "American Megatrends",       "American Megatrends copyright",    FW_PLATFORM_UNKNOWN, "AMI Aptio" },
    { "Aptio Setup",               "Aptio setup string",               FW_PLATFORM_UNKNOWN, "AMI Aptio" },
    { "InsydeH2O",                 "InsydeH2O marker",                 FW_PLATFORM_UNKNOWN, "Insyde" },
    { "Phoenix Technologies",      "Phoenix copyright",                FW_PLATFORM_UNKNOWN, "Phoenix" },
    { NULL, NULL, 0, NULL }
};

void FirmwareScanner::detectPlatform(FirmwareReport & report) const
{
    bool intel = false;
    bool amd = false;

    // Regions first: a flash descriptor with a Management Engine region is
    // about as Intel as an image gets.
    if (report.hasDescriptor && report.hasMeRegion) {
        intel = true;
        report.platformEvidence.append(
            QObject::tr("flash descriptor with a Management Engine region"));
    }
    else if (report.hasDescriptor) {
        report.platformEvidence.append(
            QObject::tr("flash descriptor present, no Management Engine region"));
    }

    // Module names.
    QStringList intelHits;
    QStringList amdHits;
    for (int i = 0; i < report.modules.count(); i++) {
        const QString & name = report.modules.at(i).name;
        if (name.isEmpty())
            continue;
        for (int h = 0; INTEL_MODULE_HINTS[h]; h++) {
            if (name.contains(INTEL_MODULE_HINTS[h], Qt::CaseInsensitive)
                    && !intelHits.contains(name)) {
                intelHits.append(name);
            }
        }
        for (int h = 0; AMD_MODULE_HINTS[h]; h++) {
            if (name.contains(AMD_MODULE_HINTS[h], Qt::CaseInsensitive)
                    && !amdHits.contains(name)) {
                amdHits.append(name);
            }
        }
    }

    if (!intelHits.isEmpty()) {
        intel = true;
        report.platformEvidence.append(QObject::tr("%1 Intel platform module(s), such as %2")
                                       .arg(intelHits.count())
                                       .arg(QStringList(intelHits.mid(0, 3)).join(", ")));
    }
    if (!amdHits.isEmpty()) {
        amd = true;
        report.platformEvidence.append(QObject::tr("%1 AMD platform module(s), such as %2")
                                       .arg(amdHits.count())
                                       .arg(QStringList(amdHits.mid(0, 3)).join(", ")));
    }

    if (intel && amd) {
        report.platform = FW_PLATFORM_BOTH;
        report.platformName = QObject::tr("ambiguous, evidence for both Intel and AMD");
    }
    else if (intel) {
        report.platform = FW_PLATFORM_INTEL;
        report.platformName = QObject::tr("Intel");
    }
    else if (amd) {
        report.platform = FW_PLATFORM_AMD;
        report.platformName = QObject::tr("AMD");
    }
    else {
        report.platform = FW_PLATFORM_UNKNOWN;
        report.platformName = QObject::tr("could not be determined");
        report.platformEvidence.append(
            QObject::tr("no platform specific module names and no flash descriptor"));
    }
}

void FirmwareScanner::detectVendor(FirmwareReport & report) const
{
    // One pass over the module bodies, looking for every signature at once.
    QSet<int> found;
    QModelIndexList files;
    collectFiles(model, model->index(0, 0), files);

    for (int f = 0; f < files.count(); f++) {
        const QByteArray blob = fileBlob(model, files.at(f));
        if (blob.isEmpty())
            continue;
        for (int s = 0; SIGNATURES[s].needle; s++) {
            if (found.contains(s))
                continue;
            if (blob.contains(SIGNATURES[s].needle))
                found.insert(s);
        }
    }

    for (int s = 0; SIGNATURES[s].needle; s++) {
        if (!found.contains(s))
            continue;

        if (SIGNATURES[s].vendor) {
            if (report.vendor.isEmpty())
                report.vendor = QString(SIGNATURES[s].vendor);
            report.vendorEvidence.append(QString(SIGNATURES[s].meaning));
        }
        else if (SIGNATURES[s].platform == FW_PLATFORM_AMD
                 || SIGNATURES[s].platform == FW_PLATFORM_INTEL) {
            report.platformEvidence.append(QString(SIGNATURES[s].meaning));
            // A signature can settle a platform the module names could not.
            if (report.platform == FW_PLATFORM_UNKNOWN) {
                report.platform = SIGNATURES[s].platform;
                report.platformName = SIGNATURES[s].platform == FW_PLATFORM_AMD
                        ? QObject::tr("AMD") : QObject::tr("Intel");
            }
        }
    }

    // Module name fallbacks for vendors with no distinctive copyright string
    // left in the image.
    if (report.vendor.isEmpty()) {
        for (int i = 0; i < report.modules.count(); i++) {
            const QString & name = report.modules.at(i).name;
            if (name.startsWith("Ami", Qt::CaseInsensitive) || name == "AMITSE") {
                report.vendor = QObject::tr("AMI Aptio");
                report.vendorEvidence.append(QObject::tr("module named %1").arg(name));
                break;
            }
            if (name.startsWith("H2O", Qt::CaseInsensitive)) {
                report.vendor = QObject::tr("Insyde");
                report.vendorEvidence.append(QObject::tr("module named %1").arg(name));
                break;
            }
        }
    }

    if (report.vendor.isEmpty())
        report.vendor = QObject::tr("could not be determined");
}

void FirmwareScanner::collectApriori(FirmwareReport & report) const
{
    QModelIndexList files;
    collectFiles(model, model->index(0, 0), files);

    for (int f = 0; f < files.count(); f++) {
        const QModelIndex & fileIndex = files.at(f);
        const QByteArray guid = model->header(fileIndex).left(sizeof(EFI_GUID));
        if (guid != EFI_PEI_APRIORI_FILE_GUID && guid != EFI_DXE_APRIORI_FILE_GUID)
            continue;

        // An apriori file is a flat list of 16 byte file GUIDs in the order
        // the dispatcher must run them.
        const QByteArray blob = fileBlob(model, fileIndex);
        for (int offset = 0; offset + (int)sizeof(EFI_GUID) <= blob.size();
             offset += sizeof(EFI_GUID)) {
            const EFI_GUID* entry = (const EFI_GUID*)(blob.constData() + offset);
            const QString text = guidToQString(*entry);
            if (!report.aprioriGuids.contains(text))
                report.aprioriGuids.append(text);
        }
    }
}

FirmwareReport FirmwareScanner::scan() const
{
    FirmwareReport report;
    if (!model || !ffsEngine)
        return report;

    const QModelIndex root = model->index(0, 0);
    if (!root.isValid())
        return report;

    walk(root, report, QObject::tr("image"));
    detectPlatform(report);
    detectVendor(report);
    collectApriori(report);
    return report;
}

QStringList FirmwareReport::summaryLines() const
{
    QStringList lines;
    lines.append(QObject::tr("Platform: %1").arg(platformName));
    for (int i = 0; i < platformEvidence.count(); i++)
        lines.append(QObject::tr("    %1").arg(platformEvidence.at(i)));

    lines.append(QObject::tr("Firmware vendor: %1").arg(vendor));
    for (int i = 0; i < vendorEvidence.count(); i++)
        lines.append(QObject::tr("    %1").arg(vendorEvidence.at(i)));

    if (!regions.isEmpty())
        lines.append(QObject::tr("Regions: %1").arg(regions.join(", ")));

    lines.append(QObject::tr("%1 volume(s), %2 module(s), %3 of them named, %4 bytes in modules")
                 .arg(volumeCount).arg(modules.count())
                 .arg(namedModuleCount).arg(totalModuleBytes));

    if (!aprioriGuids.isEmpty())
        lines.append(QObject::tr("%1 module(s) listed in apriori files").arg(aprioriGuids.count()));

    return lines;
}

// ----------------------------------------------------- preset evaluation ---

UINT8 FirmwareScanner::evaluateEntry(const PatchEntry & entry) const
{
    QModelIndexList files;
    collectFiles(model, model->index(0, 0), files);

    bool present = false;

    for (int f = 0; f < files.count(); f++) {
        const QModelIndex & fileIndex = files.at(f);
        if (model->header(fileIndex).left(sizeof(EFI_GUID)) != entry.guid)
            continue;

        present = true;

        // Look for a section of the type the patch line targets.
        for (int s = 0; s < model->rowCount(fileIndex); s++) {
            const QModelIndex section = model->index(s, 0, fileIndex);
            if (model->type(section) != Types::Section)
                continue;
            if (model->subtype(section) != entry.sectionType)
                continue;
            if (model->action(section) == Actions::Remove)
                continue;

            const QByteArray body = model->body(section);
            for (int p = 0; p < entry.patches.count(); p++) {
                const PatchData & patch = entry.patches.at(p);
                if (patch.type == PATCH_TYPE_PATTERN) {
                    if (patternOccursIn(body, patch.hexFindPattern))
                        return FW_MATCH_APPLIES;
                }
                else if (patch.type == PATCH_TYPE_OFFSET) {
                    // An offset patch has nothing to search for; if the
                    // section is long enough it will apply.
                    const int replaceLength = patch.hexReplacePattern.length() / 2;
                    if (replaceLength <= body.length()
                            && (qint64)patch.offset <= (qint64)(body.length() - replaceLength))
                        return FW_MATCH_APPLIES;
                }
            }
        }
    }

    return present ? FW_MATCH_PRESENT : FW_MATCH_ABSENT;
}

QMap<int, UINT8> FirmwareScanner::evaluateEntries(const PatchCatalog & catalog) const
{
    QMap<int, UINT8> results;
    const QVector<PatchEntry> & entries = catalog.entries();
    for (int i = 0; i < entries.count(); i++)
        results.insert(i, evaluateEntry(entries.at(i)));
    return results;
}

QMap<QString, UINT8> FirmwareScanner::evaluatePresets(const PatchCatalog & catalog) const
{
    const QMap<int, UINT8> perEntry = evaluateEntries(catalog);

    QMap<QString, UINT8> results;
    const QVector<PatchSet> & sets = catalog.sets();
    for (int s = 0; s < sets.count(); s++) {
        const PatchSet & set = sets.at(s);
        UINT8 best = FW_MATCH_ABSENT;
        for (int e = 0; e < set.entries.count(); e++) {
            const UINT8 outcome = perEntry.value(set.entries.at(e), FW_MATCH_ABSENT);
            if (outcome > best)
                best = outcome;
        }
        results.insert(set.id, best);
    }
    return results;
}

// --------------------------------------------------- debloat candidates ---

QVector<DebloatCandidate> FirmwareScanner::findCandidates(const DebloatCatalog & catalog,
                                                          const FirmwareReport & report) const
{
    QVector<DebloatCandidate> candidates;
    const QVector<DebloatEntry> & entries = catalog.entries();

    // Match catalogue entries against the modules actually in the image.
    for (int m = 0; m < report.modules.count(); m++) {
        const ModuleInfo & module = report.modules.at(m);

        for (int e = 0; e < entries.count(); e++) {
            const DebloatEntry & entry = entries.at(e);

            bool matched = false;
            if (entry.byGuid) {
                matched = (module.guid.compare(entry.match, Qt::CaseInsensitive) == 0);
            }
            else if (!module.name.isEmpty()) {
                matched = module.name.contains(entry.match, Qt::CaseInsensitive);
            }
            if (!matched)
                continue;

            DebloatCandidate candidate;
            candidate.module = module;
            candidate.categoryId = entry.categoryId;
            candidate.description = entry.description;
            candidate.inApriori = report.aprioriGuids.contains(module.guid, Qt::CaseInsensitive);
            if (candidate.inApriori)
                candidate.aprioriName = QObject::tr("an apriori dispatch list");
            candidates.append(candidate);
            break;      // first catalogue entry wins, so nothing is listed twice
        }
    }

    if (candidates.isEmpty())
        return candidates;

    // Cross reference: does any other module carry this one's GUID in its
    // body? A dependency expression records protocol GUIDs rather than file
    // GUIDs, so it can not answer this; a raw GUID in another module's code
    // or data usually means something looks this module up by file GUID.
    QModelIndexList files;
    collectFiles(model, model->index(0, 0), files);

    for (int f = 0; f < files.count(); f++) {
        const QModelIndex & fileIndex = files.at(f);
        const QByteArray ownGuid = model->header(fileIndex).left(sizeof(EFI_GUID));
        const QByteArray blob = fileBlob(model, fileIndex);
        if (blob.isEmpty())
            continue;

        const QString ownName = model->text(fileIndex).trimmed();
        const QString label = ownName.isEmpty() ? model->name(fileIndex) : ownName;

        for (int c = 0; c < candidates.count(); c++) {
            DebloatCandidate & candidate = candidates[c];
            if (candidate.module.guidBytes.isEmpty())
                continue;
            if (ownGuid == candidate.module.guidBytes)
                continue;   // a module containing its own GUID proves nothing
            if (candidate.referencedBy.count() >= MAX_REFERENCES_REPORTED)
                continue;
            if (!blob.contains(candidate.module.guidBytes))
                continue;
            if (!candidate.referencedBy.contains(label))
                candidate.referencedBy.append(label);
        }
    }

    // Is this the only module of its file type left? Removing the last DXE
    // core or the last PEI core is a special kind of mistake.
    for (int c = 0; c < candidates.count(); c++) {
        DebloatCandidate & candidate = candidates[c];
        int sameType = 0;
        for (int m = 0; m < report.modules.count(); m++) {
            if (report.modules.at(m).fileType == candidate.module.fileType)
                sameType++;
        }
        candidate.isLastOfKind = (sameType <= 1);
    }

    return candidates;
}
