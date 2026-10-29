/* uefipatch.cpp

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QCryptographicHash>
#include <QFile>
#include <iostream>

#include "uefipatch.h"

#define DEFAULT_SET_ID "default"

PatchEntry::PatchEntry() :
    sectionType(0), lineNumber(0), status(PATCH_STATUS_NOT_RUN), sectionsPatched(0)
{
}

PatchSet::PatchSet()
{
}

PatchOptions::PatchOptions() :
    verbose(false), dryRun(false), strict(false), allCopies(false),
    force(false), hashes(false)
{
}

static void out(const QString & line)
{
    std::cout << line.toLocal8Bit().constData() << std::endl;
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

static QString digestName()
{
#if QT_VERSION >= 0x050000
    return QObject::tr("SHA-256");
#else
    return QObject::tr("SHA-1");
#endif
}

static QString digestOf(const QByteArray & data)
{
#if QT_VERSION >= 0x050000
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
#else
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
#endif
    return QString(hash.toHex()).toUpper();
}

static QString statusTag(const UINT8 status)
{
    switch (status) {
    case PATCH_STATUS_APPLIED: return QObject::tr("applied");
    case PATCH_STATUS_MISSED:  return QObject::tr(" missed");
    case PATCH_STATUS_SKIPPED: return QObject::tr("skipped");
    default:                   return QObject::tr(" not run");
    }
}

UEFIPatch::UEFIPatch(QObject *parent) :
    QObject(parent)
{
    ffsEngine = new FfsEngine(this);
    model = ffsEngine->treeModel();
}

UEFIPatch::~UEFIPatch()
{
    delete ffsEngine;
}

QString UEFIPatch::lastError() const
{
    return errorText;
}

QStringList UEFIPatch::lastWarnings() const
{
    return warnings;
}

PatchSet & UEFIPatch::currentSet(const QString & id)
{
    for (int i = 0; i < sets.count(); i++) {
        if (sets.at(i).id == id)
            return sets[i];
    }

    PatchSet set;
    set.id = id;
    sets.append(set);
    return sets[sets.count() - 1];
}

UINT8 UEFIPatch::loadPatchFile(const QString & path)
{
    entries.clear();
    sets.clear();
    warnings.clear();
    errorText.clear();
    sourceName = path;

    const QFileInfo patchInfo = QFileInfo(path);
    if (!patchInfo.exists()) {
        errorText = tr("%1 does not exist").arg(path);
        return ERR_INVALID_FILE;
    }

    QFile file;
    file.setFileName(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        errorText = tr("%1 can not be read").arg(path);
        return ERR_INVALID_FILE;
    }

    QString setId = DEFAULT_SET_ID;
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
            // the last comment seen becomes the label of the next patch line.
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
                        errorText = tr("%1 line %2: #!set needs a set name").arg(path).arg(lineNumber);
                        return ERR_INVALID_PARAMETER;
                    }
                    setId = rest.at(0);
                    currentSet(setId);
                }
                else if (directive == "desc" || directive == "description") {
                    currentSet(setId).description = argument;
                }
                else if (directive == "risk") {
                    currentSet(setId).risk = rest.isEmpty() ? QString() : rest.at(0);
                    if (rest.count() > 1) {
                        warnings.append(tr("%1 line %2: #!risk takes one word, "
                                           "the rest belongs in #!note")
                                        .arg(path).arg(lineNumber));
                    }
                }
                else if (directive == "note") {
                    PatchSet & set = currentSet(setId);
                    set.note = set.note.isEmpty() ? argument : set.note + " " + argument;
                }
                else {
                    warnings.append(tr("%1 line %2: unknown directive #!%3, ignored")
                                    .arg(path).arg(lineNumber).arg(QString::fromUtf8(directive)));
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
        const UINT8 result = parsePatchLine(line, lineNumber, setId, label, entry);
        if (result)
            return result;

        currentSet(setId).entries.append(entries.count());
        entries.append(entry);
        // The label is kept, not cleared: patches.txt puts one comment above a
        // run of related lines, and each of them should report under it.
    }

    file.close();

    if (entries.isEmpty()) {
        errorText = tr("%1 contains no patches").arg(path);
        return ERR_INVALID_FILE;
    }

    return ERR_SUCCESS;
}

UINT8 UEFIPatch::loadPatchArgument(const QString & patch)
{
    entries.clear();
    sets.clear();
    warnings.clear();
    errorText.clear();
    sourceName = tr("command line");

    if (patch.isEmpty()) {
        errorText = tr("empty patch argument");
        return ERR_INVALID_PARAMETER;
    }

    PatchEntry entry;
    const UINT8 result = parsePatchLine(patch.toUtf8().trimmed(), 1, DEFAULT_SET_ID, QString(), entry);
    if (result)
        return result;

    currentSet(DEFAULT_SET_ID).entries.append(0);
    entries.append(entry);
    return ERR_SUCCESS;
}

UINT8 UEFIPatch::parsePatchLine(const QByteArray & line, UINT32 lineNumber,
                                const QString & setId, const QString & label,
                                PatchEntry & entry)
{
    const QList<QByteArray> fields = splitFields(line);
    if (fields.count() < 3) {
        errorText = tr("%1 line %2: expected \"FileGuid SectionType Patch\", got %3 field(s)")
                .arg(sourceName).arg(lineNumber).arg(fields.count());
        return ERR_INVALID_PARAMETER;
    }

    const QString guidText = QString::fromUtf8(fields.at(0));
    const QUuid uuid = QUuid(guidText);
    if (uuid.isNull()) {
        errorText = tr("%1 line %2: \"%3\" is not a usable file GUID")
                .arg(sourceName).arg(lineNumber).arg(guidText);
        return ERR_INVALID_PARAMETER;
    }

    bool converted = false;
    const UINT16 sectionType = fields.at(1).toUShort(&converted, 16);
    if (!converted || sectionType > 0xFF) {
        errorText = tr("%1 line %2: \"%3\" is not a one byte hexadecimal section type")
                .arg(sourceName).arg(lineNumber).arg(QString::fromUtf8(fields.at(1)));
        return ERR_INVALID_PARAMETER;
    }
    if (!isDocumentedSectionType((UINT8)sectionType)) {
        warnings.append(tr("%1 line %2: section type %3h is not one of the documented ones, "
                           "patching it may break the image")
                        .arg(sourceName).arg(lineNumber)
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
        const UINT8 result = parsePatchSpec(fields.at(i), lineNumber, patch);
        if (result)
            return result;
        entry.patches.append(patch);
    }

    if (entry.patches.isEmpty()) {
        errorText = tr("%1 line %2: no patch operations on this line")
                .arg(sourceName).arg(lineNumber);
        return ERR_INVALID_PARAMETER;
    }

    return ERR_SUCCESS;
}

UINT8 UEFIPatch::parsePatchSpec(const QByteArray & spec, UINT32 lineNumber, PatchData & patch)
{
    const QList<QByteArray> parts = spec.split(':');
    if (parts.count() != 3) {
        errorText = tr("%1 line %2: \"%3\" is not \"Type:Find:Replace\" or \"Type:Offset:Replace\"")
                .arg(sourceName).arg(lineNumber).arg(QString::fromUtf8(spec));
        return ERR_INVALID_PARAMETER;
    }

    if (parts.at(0).length() != 1) {
        errorText = tr("%1 line %2: \"%3\" is not a single character patch type")
                .arg(sourceName).arg(lineNumber).arg(QString::fromUtf8(parts.at(0)));
        return ERR_UNKNOWN_PATCH_TYPE;
    }

    const char type = (char)QChar(parts.at(0).at(0)).toUpper().toLatin1();
    const QByteArray replacePattern = parts.at(2);

    if (!isValidHexPattern(replacePattern)) {
        errorText = tr("%1 line %2: \"%3\" is not an even length hexadecimal replace pattern")
                .arg(sourceName).arg(lineNumber).arg(QString::fromUtf8(replacePattern));
        return ERR_INVALID_SYMBOL;
    }

    if (type == PATCH_TYPE_PATTERN) {
        const QByteArray findPattern = parts.at(1);
        if (!isValidHexPattern(findPattern)) {
            errorText = tr("%1 line %2: \"%3\" is not an even length hexadecimal find pattern")
                    .arg(sourceName).arg(lineNumber).arg(QString::fromUtf8(findPattern));
            return ERR_INVALID_SYMBOL;
        }
        if (findPattern.length() != replacePattern.length()) {
            warnings.append(tr("%1 line %2: replace pattern is %3 byte(s) where the find pattern "
                               "is %4, bytes outside the shorter one are left as they are or "
                               "overwritten")
                            .arg(sourceName).arg(lineNumber)
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
            errorText = tr("%1 line %2: \"%3\" is not a hexadecimal offset")
                    .arg(sourceName).arg(lineNumber).arg(QString::fromUtf8(parts.at(1)));
            return ERR_INVALID_PARAMETER;
        }
        patch.type = PATCH_TYPE_OFFSET;
        patch.offset = offset;
        patch.hexFindPattern.clear();
        patch.hexReplacePattern = replacePattern;
        return ERR_SUCCESS;
    }

    errorText = tr("%1 line %2: \"%3\" is not a known patch type, expected P or O")
            .arg(sourceName).arg(lineNumber).arg(QString(QChar(type)));
    return ERR_UNKNOWN_PATCH_TYPE;
}

void UEFIPatch::listSets() const
{
    out(tr("Patch sets in %1:").arg(sourceName));

    for (int i = 0; i < sets.count(); i++) {
        const PatchSet & set = sets.at(i);
        if (set.entries.isEmpty())
            continue;

        QString head = QString("  %1  %2")
                .arg(set.id, -28)
                .arg(tr("%1 patch line(s)").arg(set.entries.count()));
        if (!set.risk.isEmpty())
            head += tr(", risk: %1").arg(set.risk);

        out(QString());
        out(head);
        if (!set.description.isEmpty())
            out(QString("      %1").arg(set.description));
        if (!set.note.isEmpty())
            out(QString("      %1").arg(tr("Note: %1").arg(set.note)));
    }

    out(QString());
    out(tr("Select sets with --set a,b and skip them with --exclude a,b."));
}

bool UEFIPatch::isSelected(const PatchEntry & entry, const PatchOptions & options) const
{
    if (options.excludeSets.contains(entry.setId, Qt::CaseInsensitive))
        return false;
    if (options.includeSets.isEmpty())
        return true;
    return options.includeSets.contains(entry.setId, Qt::CaseInsensitive);
}

UINT8 UEFIPatch::run(const QString & path, const QString & outputPath, const PatchOptions & options)
{
    if (entries.isEmpty()) {
        errorText = tr("no patches loaded");
        return ERR_INVALID_PARAMETER;
    }

    const QFileInfo inputInfo = QFileInfo(path);
    if (!inputInfo.exists()) {
        errorText = tr("%1 does not exist").arg(path);
        return ERR_FILE_OPEN;
    }

    // Writing over the input would destroy the only copy of a firmware dump,
    // which is exactly the file that is hardest to get back.
    const QFileInfo outputInfo = QFileInfo(outputPath);
    if (!options.force && outputInfo.exists()
            && outputInfo.canonicalFilePath() == inputInfo.canonicalFilePath()) {
        errorText = tr("output would overwrite the input image, pass --force to allow it");
        return ERR_INVALID_PARAMETER;
    }

    QFile inputFile;
    inputFile.setFileName(path);
    if (!inputFile.open(QFile::ReadOnly)) {
        errorText = tr("%1 can not be read").arg(path);
        return ERR_FILE_READ;
    }
    const QByteArray buffer = inputFile.readAll();
    inputFile.close();

    out(tr("Input:   %1 (%2 bytes)").arg(path).arg(buffer.size()));
    if (options.hashes)
        out(tr("         %1 %2").arg(digestName()).arg(digestOf(buffer)));

    UINT8 result = ffsEngine->parseImageFile(buffer);
    if (result) {
        errorText = tr("%1 could not be parsed as a UEFI image").arg(path);
        return result;
    }

    UINT32 selectedCount = 0;
    for (int i = 0; i < entries.count(); i++) {
        PatchEntry & entry = entries[i];
        if (!isSelected(entry, options)) {
            entry.status = PATCH_STATUS_SKIPPED;
            continue;
        }
        selectedCount++;
        result = applyEntry(entry, options);
        if (result && result != ERR_NOTHING_TO_PATCH)
            return result;
    }

    if (selectedCount == 0) {
        errorText = tr("the --set and --exclude options left no patches to apply");
        return ERR_INVALID_PARAMETER;
    }

    QByteArray reconstructed;
    result = ffsEngine->reconstructImageFile(reconstructed);
    if (result) {
        errorText = tr("the patched image could not be reconstructed");
        return result;
    }

    printReport(options);

    UINT32 applied = 0;
    UINT32 missed = 0;
    for (int i = 0; i < entries.count(); i++) {
        if (entries.at(i).status == PATCH_STATUS_APPLIED)
            applied++;
        else if (entries.at(i).status == PATCH_STATUS_MISSED)
            missed++;
    }

    if (reconstructed == buffer) {
        out(tr("Result:  the image is unchanged, nothing was written"));
        return ERR_NOTHING_TO_PATCH;
    }

    if (options.dryRun) {
        out(tr("Output:  %1 bytes would be written to %2 (dry run, nothing written)")
            .arg(reconstructed.size()).arg(outputPath));
        if (options.hashes)
            out(tr("         %1 %2").arg(digestName()).arg(digestOf(reconstructed)));
    }
    else {
        QFile outputFile;
        outputFile.setFileName(outputPath);
        if (!outputFile.open(QFile::WriteOnly)) {
            errorText = tr("%1 can not be written").arg(outputPath);
            return ERR_FILE_WRITE;
        }
        outputFile.resize(0);
        const qint64 written = outputFile.write(reconstructed);
        outputFile.close();
        if (written != (qint64)reconstructed.size()) {
            errorText = tr("only %1 of %2 bytes were written to %3")
                    .arg(written).arg(reconstructed.size()).arg(outputPath);
            return ERR_FILE_WRITE;
        }

        out(tr("Output:  %1 (%2 bytes)").arg(outputPath).arg(reconstructed.size()));
        if (options.hashes)
            out(tr("         %1 %2").arg(digestName()).arg(digestOf(reconstructed)));
    }

    // Reporting a clean exit when the one patch that mattered did not match is
    // how a half patched image ends up on a flash chip, so --strict says so.
    if (options.strict && missed > 0) {
        errorText = tr("%1 of %2 selected patch line(s) did not match (--strict)")
                .arg(missed).arg(applied + missed);
        return ERR_NOTHING_TO_PATCH;
    }

    return ERR_SUCCESS;
}

UINT8 UEFIPatch::applyEntry(PatchEntry & entry, const PatchOptions & options)
{
    UINT32 sectionsPatched = 0;
    const UINT8 result = patchSections(model->index(0, 0), entry.guid, entry.sectionType,
                                       entry.patches, options.allCopies, sectionsPatched);
    if (result && result != ERR_NOTHING_TO_PATCH) {
        errorText = tr("%1 line %2: %3").arg(sourceName).arg(entry.lineNumber).arg(errorMessage(result));
        entry.status = PATCH_STATUS_MISSED;
        return result;
    }

    entry.sectionsPatched = sectionsPatched;
    entry.status = sectionsPatched > 0 ? PATCH_STATUS_APPLIED : PATCH_STATUS_MISSED;
    return ERR_SUCCESS;
}

// Walks the whole tree and counts what was changed. Upstream broke out of the
// recursion on the first success and then returned ERR_NOTHING_TO_PATCH
// anyway, so a caller could never tell a hit from a miss; the count is what
// makes per line reporting and --strict possible.
UINT8 UEFIPatch::patchSections(const QModelIndex & index, const QByteArray & fileGuid,
                               const UINT8 sectionType, const QVector<PatchData> & patches,
                               bool allCopies, UINT32 & sectionsPatched)
{
    if (!model || !index.isValid())
        return ERR_INVALID_PARAMETER;

    if (model->type(index) == Types::Section && model->subtype(index) == sectionType) {
        const QModelIndex fileIndex = model->findParentOfType(index, Types::File);
        if (model->type(fileIndex) == Types::File
                && model->header(fileIndex).left(sizeof(EFI_GUID)) == fileGuid) {
            const UINT8 result = ffsEngine->patch(index, patches);
            if (result == ERR_SUCCESS)
                sectionsPatched++;
            else if (result != ERR_NOTHING_TO_PATCH)
                return result;
            // A matching section has no children to walk into.
            return ERR_SUCCESS;
        }
    }

    for (int i = 0; i < model->rowCount(index); i++) {
        const UINT8 result = patchSections(model->index(i, 0, index), fileGuid, sectionType,
                                           patches, allCopies, sectionsPatched);
        if (result && result != ERR_NOTHING_TO_PATCH)
            return result;
        if (sectionsPatched > 0 && !allCopies)
            break;
    }

    return ERR_SUCCESS;
}

void UEFIPatch::printReport(const PatchOptions & options) const
{
    UINT32 applied = 0;
    UINT32 missed = 0;
    UINT32 skipped = 0;
    UINT32 sections = 0;

    for (int i = 0; i < entries.count(); i++) {
        const PatchEntry & entry = entries.at(i);
        switch (entry.status) {
        case PATCH_STATUS_APPLIED: applied++; sections += entry.sectionsPatched; break;
        case PATCH_STATUS_MISSED:  missed++; break;
        case PATCH_STATUS_SKIPPED: skipped++; break;
        default: break;
        }
    }

    if (options.verbose) {
        for (int s = 0; s < sets.count(); s++) {
            const PatchSet & set = sets.at(s);
            if (set.entries.isEmpty())
                continue;

            bool headerPrinted = false;
            for (int e = 0; e < set.entries.count(); e++) {
                const PatchEntry & entry = entries.at(set.entries.at(e));
                if (entry.status == PATCH_STATUS_SKIPPED)
                    continue;

                if (!headerPrinted) {
                    QString header = tr("  set %1").arg(set.id);
                    if (!set.risk.isEmpty())
                        header += tr(" (risk: %1)").arg(set.risk);
                    if (!set.description.isEmpty())
                        header += tr("  %1").arg(set.description);
                    out(QString());
                    out(header);
                    headerPrinted = true;
                }

                QString detail = entry.label;
                if (entry.status == PATCH_STATUS_APPLIED && entry.sectionsPatched > 1)
                    detail += tr(" [%1 sections]").arg(entry.sectionsPatched);

                out(QString("    [%1] line %2  %3  sect %4  %5")
                    .arg(statusTag(entry.status))
                    .arg(entry.lineNumber, 4)
                    .arg(entry.guidText.toUpper(), -36)
                    .arg(QString::number(entry.sectionType, 16).toUpper().rightJustified(2, '0'))
                    .arg(detail));
            }
        }
        out(QString());
    }

    QString summary = tr("Patches: %1 applied, %2 not matched").arg(applied).arg(missed);
    if (skipped > 0)
        summary += tr(", %1 skipped").arg(skipped);
    summary += tr(" (%1 section(s) changed)").arg(sections);
    out(summary);

    if (missed > 0 && !options.verbose)
        out(tr("         pass --verbose to see which patch lines did not match"));
}

// Thin wrappers so that older call sites keep the behaviour they expect.
UINT8 UEFIPatch::patchFromFile(const QString & path, const QString & patches, const QString & outputPath)
{
    const UINT8 result = loadPatchFile(patches);
    if (result)
        return result;
    return run(path, outputPath, PatchOptions());
}

UINT8 UEFIPatch::patchFromArg(const QString & path, const QString & patch, const QString & outputPath)
{
    const UINT8 result = loadPatchArgument(patch);
    if (result)
        return result;
    return run(path, outputPath, PatchOptions());
}
