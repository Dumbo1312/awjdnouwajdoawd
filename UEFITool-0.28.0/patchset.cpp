/* patchset.cpp

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QFile>
#include <QObject>
#include <QUuid>

#include "patchset.h"

#define DEFAULT_SET_ID "default"

PatchEntry::PatchEntry() :
    sectionType(0), lineNumber(0), status(PATCH_STATUS_NOT_RUN), sectionsPatched(0)
{
}

PatchSet::PatchSet() :
    platform(PATCH_PLATFORM_ANY)
{
}

bool PatchSet::isApplicable() const
{
    return !entries.isEmpty();
}

// Section types the patch file format documents as safe to target. Anything
// else is accepted but called out, because patching a section the engine
// reconstructs differently is a good way to produce an unbootable image.
static bool isDocumentedSectionType(const UINT8 type)
{
    switch (type) {
    case EFI_SECTION_PE32:
    case EFI_SECTION_PIC:
    case EFI_SECTION_TE:
    case EFI_SECTION_DXE_DEPEX:
    case EFI_SECTION_VERSION:
    case EFI_SECTION_USER_INTERFACE:
    case EFI_SECTION_COMPATIBILITY16:
    case EFI_SECTION_FREEFORM_SUBTYPE_GUID:
    case EFI_SECTION_RAW:
    case EFI_SECTION_PEI_DEPEX:
    case EFI_SECTION_SMM_DEPEX:
        return true;
    default:
        return false;
    }
}

// EFI_GUID wire layout: Data1 and Data2/Data3 little endian, Data4 as written.
// Built field by field so that neither QUuid's internal layout nor the
// lifetime of a fromRawData() buffer matters.
static QByteArray guidToBytes(const QUuid & uuid)
{
    QByteArray guid;
    guid.resize(sizeof(EFI_GUID));
    UINT8* data = (UINT8*)guid.data();
    data[0] = (UINT8)(uuid.data1 & 0xFF);
    data[1] = (UINT8)((uuid.data1 >> 8) & 0xFF);
    data[2] = (UINT8)((uuid.data1 >> 16) & 0xFF);
    data[3] = (UINT8)((uuid.data1 >> 24) & 0xFF);
    data[4] = (UINT8)(uuid.data2 & 0xFF);
    data[5] = (UINT8)((uuid.data2 >> 8) & 0xFF);
    data[6] = (UINT8)(uuid.data3 & 0xFF);
    data[7] = (UINT8)((uuid.data3 >> 8) & 0xFF);
    for (int i = 0; i < 8; i++)
        data[8 + i] = uuid.data4[i];
    return guid;
}

// Splits on runs of blanks and drops empty fields, so that a trailing space,
// a tab or a double space in a patch file is not a field of its own.
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

// A hex pattern is an even length string of hex digits, optionally with '.'
// standing for "any nibble".
static bool isValidHexPattern(const QByteArray & pattern)
{
    if (pattern.isEmpty() || pattern.length() % 2 != 0)
        return false;

    for (int i = 0; i < pattern.length(); i++) {
        const char c = pattern.at(i);
        const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHex && c != '.')
            return false;
    }
    return true;
}

PatchCatalog::PatchCatalog()
{
}

void PatchCatalog::clear(const QString & newSource)
{
    patchEntries.clear();
    patchSets.clear();
    warningList.clear();
    errorText.clear();
    source = newSource;
}

void PatchCatalog::resetStatus()
{
    for (int i = 0; i < patchEntries.count(); i++) {
        patchEntries[i].status = PATCH_STATUS_NOT_RUN;
        patchEntries[i].sectionsPatched = 0;
    }
}

QStringList PatchCatalog::platforms() const
{
    QStringList names;
    for (int i = 0; i < patchSets.count(); i++) {
        const QString & platform = patchSets.at(i).platform;
        if (!names.contains(platform))
            names.append(platform);
    }
    return names;
}

const PatchSet* PatchCatalog::setById(const QString & id) const
{
    for (int i = 0; i < patchSets.count(); i++) {
        if (patchSets.at(i).id == id)
            return &patchSets.at(i);
    }
    return NULL;
}

PatchSet & PatchCatalog::currentSet(const QString & id)
{
    for (int i = 0; i < patchSets.count(); i++) {
        if (patchSets.at(i).id == id)
            return patchSets[i];
    }

    PatchSet set;
    set.id = id;
    patchSets.append(set);
    return patchSets[patchSets.count() - 1];
}

UINT8 PatchCatalog::loadFile(const QString & path)
{
    clear(path);

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

    QString setId = DEFAULT_SET_ID;
    QString platform = PATCH_PLATFORM_ANY;
    QString label;
    UINT32 lineNumber = 0;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        lineNumber++;

        if (line.isEmpty()) {
            label.clear();
            continue;
        }

        if (line.at(0) == '#') {
            // "#!" introduces a directive. Everything else is a comment, and
            // the last comment seen becomes the label of the next patch lines.
            if (line.startsWith("#!")) {
                const QList<QByteArray> fields = splitFields(line.mid(2));
                if (fields.isEmpty())
                    continue;

                const QByteArray directive = fields.at(0).toLower();

                // Everything after the directive word, with blank runs collapsed.
                QStringList rest;
                for (int f = 1; f < fields.count(); f++)
                    rest.append(QString::fromUtf8(fields.at(f)));
                const QString argument = rest.join(" ");

                if (directive == "set") {
                    if (rest.isEmpty()) {
                        errorText = QObject::tr("%1 line %2: #!set needs a set name")
                                .arg(path).arg(lineNumber);
                        return ERR_INVALID_PARAMETER;
                    }
                    setId = rest.at(0);
                    // A set inherits the platform block it was declared in.
                    currentSet(setId).platform = platform;
                }
                else if (directive == "platform") {
                    if (rest.isEmpty()) {
                        errorText = QObject::tr("%1 line %2: #!platform needs a name")
                                .arg(path).arg(lineNumber);
                        return ERR_INVALID_PARAMETER;
                    }
                    platform = argument;
                }
                else if (directive == "desc" || directive == "description") {
                    currentSet(setId).description = argument;
                }
                else if (directive == "risk") {
                    currentSet(setId).risk = rest.isEmpty() ? QString() : rest.at(0);
                    if (rest.count() > 1) {
                        warningList.append(QObject::tr("%1 line %2: #!risk takes one word, "
                                                       "the rest belongs in #!note")
                                           .arg(path).arg(lineNumber));
                    }
                }
                else if (directive == "note") {
                    PatchSet & set = currentSet(setId);
                    set.note = set.note.isEmpty() ? argument : set.note + " " + argument;
                }
                else if (directive == "info") {
                    PatchSet & set = currentSet(setId);
                    set.info = set.info.isEmpty() ? argument : set.info + " " + argument;
                }
                else {
                    warningList.append(QObject::tr("%1 line %2: unknown directive #!%3, ignored")
                                       .arg(path).arg(lineNumber)
                                       .arg(QString::fromUtf8(directive)));
                }
                continue;
            }

            // Strip the '#' and any following blanks to get a readable label.
            label = QString::fromUtf8(line.mid(1).trimmed());
            // Separator rules of dashes or asterisks are decoration, not labels.
            bool decorationOnly = !label.isEmpty();
            for (int c = 0; c < label.length() && decorationOnly; c++) {
                const QChar ch = label.at(c);
                if (ch != '-' && ch != '=' && ch != '*' && ch != '_' && ch != ' ')
                    decorationOnly = false;
            }
            if (decorationOnly)
                label.clear();
            continue;
        }

        PatchEntry entry;
        const UINT8 result = parseLine(line, lineNumber, setId, label, entry);
        if (result)
            return result;

        currentSet(setId).entries.append(patchEntries.count());
        patchEntries.append(entry);
        // The label is kept, not cleared: patch files put one comment above a
        // run of related lines, and each of them should report under it.
    }

    file.close();

    // A set with no patch lines is only meaningful if it explains itself with
    // #!info; the GUI shows those as notes rather than something to click.
    for (int i = 0; i < patchSets.count(); i++) {
        const PatchSet & set = patchSets.at(i);
        if (set.entries.isEmpty() && set.info.isEmpty()) {
            warningList.append(QObject::tr("%1: set %2 has no patch lines and no #!info")
                               .arg(path).arg(set.id));
        }
    }

    if (patchEntries.isEmpty() && patchSets.isEmpty()) {
        errorText = QObject::tr("%1 contains no patches").arg(path);
        return ERR_INVALID_FILE;
    }

    return ERR_SUCCESS;
}

UINT8 PatchCatalog::loadLine(const QString & patch)
{
    clear(QObject::tr("command line"));

    if (patch.trimmed().isEmpty()) {
        errorText = QObject::tr("empty patch argument");
        return ERR_INVALID_PARAMETER;
    }

    PatchEntry entry;
    const UINT8 result = parseLine(patch.toUtf8().trimmed(), 1, DEFAULT_SET_ID, QString(), entry);
    if (result)
        return result;

    currentSet(DEFAULT_SET_ID).entries.append(0);
    patchEntries.append(entry);
    return ERR_SUCCESS;
}

UINT8 PatchCatalog::parseLine(const QByteArray & line, UINT32 lineNumber,
                              const QString & setId, const QString & label,
                              PatchEntry & entry)
{
    const QList<QByteArray> fields = splitFields(line);
    if (fields.count() < 3) {
        errorText = QObject::tr("%1 line %2: expected \"FileGuid SectionType Patch\", got %3 field(s)")
                .arg(source).arg(lineNumber).arg(fields.count());
        return ERR_INVALID_PARAMETER;
    }

    const QString guidText = QString::fromUtf8(fields.at(0));
    const QUuid uuid = QUuid(guidText);
    if (uuid.isNull()) {
        errorText = QObject::tr("%1 line %2: \"%3\" is not a usable file GUID")
                .arg(source).arg(lineNumber).arg(guidText);
        return ERR_INVALID_PARAMETER;
    }

    bool converted = false;
    const UINT16 sectionType = fields.at(1).toUShort(&converted, 16);
    if (!converted || sectionType > 0xFF) {
        errorText = QObject::tr("%1 line %2: \"%3\" is not a one byte hexadecimal section type")
                .arg(source).arg(lineNumber).arg(QString::fromUtf8(fields.at(1)));
        return ERR_INVALID_PARAMETER;
    }
    if (!isDocumentedSectionType((UINT8)sectionType)) {
        warningList.append(QObject::tr("%1 line %2: section type %3h is not one of the documented "
                                       "ones, patching it may break the image")
                           .arg(source).arg(lineNumber)
                           .arg(QString::number(sectionType, 16).toUpper()));
    }

    entry.guid = guidToBytes(uuid);
    entry.guidText = guidText;
    entry.sectionType = (UINT8)sectionType;
    entry.setId = setId;
    entry.label = label;
    entry.lineNumber = lineNumber;

    for (int i = 2; i < fields.count(); i++) {
        PatchData patch;
        const UINT8 result = parseSpec(fields.at(i), lineNumber, patch);
        if (result)
            return result;
        entry.patches.append(patch);
    }

    if (entry.patches.isEmpty()) {
        errorText = QObject::tr("%1 line %2: no patch operations on this line")
                .arg(source).arg(lineNumber);
        return ERR_INVALID_PARAMETER;
    }

    return ERR_SUCCESS;
}

UINT8 PatchCatalog::parseSpec(const QByteArray & spec, UINT32 lineNumber, PatchData & patch)
{
    const QList<QByteArray> parts = spec.split(':');
    if (parts.count() != 3) {
        errorText = QObject::tr("%1 line %2: \"%3\" is not \"Type:Find:Replace\" or \"Type:Offset:Replace\"")
                .arg(source).arg(lineNumber).arg(QString::fromUtf8(spec));
        return ERR_INVALID_PARAMETER;
    }

    if (parts.at(0).length() != 1) {
        errorText = QObject::tr("%1 line %2: \"%3\" is not a single character patch type")
                .arg(source).arg(lineNumber).arg(QString::fromUtf8(parts.at(0)));
        return ERR_UNKNOWN_PATCH_TYPE;
    }

    const char type = (char)QChar(parts.at(0).at(0)).toUpper().toLatin1();
    const QByteArray replacePattern = parts.at(2);

    if (!isValidHexPattern(replacePattern)) {
        errorText = QObject::tr("%1 line %2: \"%3\" is not an even length hexadecimal replace pattern")
                .arg(source).arg(lineNumber).arg(QString::fromUtf8(replacePattern));
        return ERR_INVALID_SYMBOL;
    }

    if (type == PATCH_TYPE_PATTERN) {
        const QByteArray findPattern = parts.at(1);
        if (!isValidHexPattern(findPattern)) {
            errorText = QObject::tr("%1 line %2: \"%3\" is not an even length hexadecimal find pattern")
                    .arg(source).arg(lineNumber).arg(QString::fromUtf8(findPattern));
            return ERR_INVALID_SYMBOL;
        }
        if (findPattern.length() != replacePattern.length()) {
            warningList.append(QObject::tr("%1 line %2: replace pattern is %3 byte(s) where the find "
                                           "pattern is %4, bytes outside the shorter one are left as "
                                           "they are or overwritten")
                               .arg(source).arg(lineNumber)
                               .arg(replacePattern.length() / 2).arg(findPattern.length() / 2));
        }
        patch.type = PATCH_TYPE_PATTERN;
        patch.offset = 0xFFFFFFFF;
        patch.hexFindPattern = findPattern;
        patch.hexReplacePattern = replacePattern;
        return ERR_SUCCESS;
    }

    if (type == PATCH_TYPE_OFFSET) {
        bool converted = false;
        const UINT32 offset = parts.at(1).toUInt(&converted, 16);
        if (!converted) {
            errorText = QObject::tr("%1 line %2: \"%3\" is not a hexadecimal offset")
                    .arg(source).arg(lineNumber).arg(QString::fromUtf8(parts.at(1)));
            return ERR_INVALID_PARAMETER;
        }
        patch.type = PATCH_TYPE_OFFSET;
        patch.offset = offset;
        patch.hexFindPattern.clear();
        patch.hexReplacePattern = replacePattern;
        return ERR_SUCCESS;
    }

    errorText = QObject::tr("%1 line %2: \"%3\" is not a known patch type, expected P or O")
            .arg(source).arg(lineNumber).arg(QString(QChar(type)));
    return ERR_UNKNOWN_PATCH_TYPE;
}

// Walks the whole tree and counts what was changed. Upstream broke out of the
// recursion on the first success and then returned ERR_NOTHING_TO_PATCH
// anyway, so a caller could never tell a hit from a miss; the count is what
// makes per line reporting, --strict and the preset list possible.
static UINT8 patchSections(FfsEngine* engine, TreeModel* model, const QModelIndex & index,
                           const QByteArray & fileGuid, const UINT8 sectionType,
                           const QVector<PatchData> & patches, bool allCopies,
                           UINT32 & sectionsPatched)
{
    if (!engine || !model || !index.isValid())
        return ERR_INVALID_PARAMETER;

    if (model->type(index) == Types::Section && model->subtype(index) == sectionType) {
        const QModelIndex fileIndex = model->findParentOfType(index, Types::File);
        if (model->type(fileIndex) == Types::File
                && model->header(fileIndex).left(sizeof(EFI_GUID)) == fileGuid) {
            const UINT8 result = engine->patch(index, patches);
            if (result == ERR_SUCCESS)
                sectionsPatched++;
            else if (result != ERR_NOTHING_TO_PATCH)
                return result;
            // A matching section has no children to walk into.
            return ERR_SUCCESS;
        }
    }

    for (int i = 0; i < model->rowCount(index); i++) {
        const UINT8 result = patchSections(engine, model, model->index(i, 0, index), fileGuid,
                                           sectionType, patches, allCopies, sectionsPatched);
        if (result && result != ERR_NOTHING_TO_PATCH)
            return result;
        if (sectionsPatched > 0 && !allCopies)
            break;
    }

    return ERR_SUCCESS;
}

UINT8 applyPatchEntry(FfsEngine* engine, TreeModel* model, PatchEntry & entry, bool allCopies)
{
    if (!engine || !model)
        return ERR_INVALID_PARAMETER;

    UINT32 sectionsPatched = 0;
    const UINT8 result = patchSections(engine, model, model->index(0, 0), entry.guid,
                                       entry.sectionType, entry.patches, allCopies,
                                       sectionsPatched);
    if (result && result != ERR_NOTHING_TO_PATCH) {
        entry.status = PATCH_STATUS_MISSED;
        return result;
    }

    entry.sectionsPatched = sectionsPatched;
    entry.status = sectionsPatched > 0 ? PATCH_STATUS_APPLIED : PATCH_STATUS_MISSED;
    return ERR_SUCCESS;
}
