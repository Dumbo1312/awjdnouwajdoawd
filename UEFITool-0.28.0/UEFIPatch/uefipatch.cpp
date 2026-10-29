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

PatchOptions::PatchOptions() :
    verbose(false), dryRun(false), strict(false), allCopies(false),
    force(false), hashes(false)
{
}

static void out(const QString & line)
{
    std::cout << line.toLocal8Bit().constData() << std::endl;
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
    return errorText.isEmpty() ? catalog.error() : errorText;
}

QStringList UEFIPatch::lastWarnings() const
{
    return catalog.warnings();
}

UINT8 UEFIPatch::loadPatchFile(const QString & path)
{
    errorText.clear();
    return catalog.loadFile(path);
}

UINT8 UEFIPatch::loadPatchArgument(const QString & patch)
{
    errorText.clear();
    return catalog.loadLine(patch);
}

void UEFIPatch::listSets() const
{
    out(tr("Patch sets in %1:").arg(catalog.sourceName()));

    const QStringList platforms = catalog.platforms();
    const QVector<PatchSet> & sets = catalog.sets();

    for (int p = 0; p < platforms.count(); p++) {
        const QString & platform = platforms.at(p);
        bool platformPrinted = false;

        for (int i = 0; i < sets.count(); i++) {
            const PatchSet & set = sets.at(i);
            if (set.platform != platform)
                continue;
            if (!set.isApplicable() && set.info.isEmpty())
                continue;

            if (!platformPrinted) {
                out(QString());
                out(tr("  [%1]").arg(platform));
                platformPrinted = true;
            }

            QString head = QString("    %1  ").arg(set.id, -28);
            if (set.isApplicable()) {
                head += tr("%1 patch line(s)").arg(set.entries.count());
                if (!set.risk.isEmpty())
                    head += tr(", risk: %1").arg(set.risk);
            }
            else {
                head += tr("nothing to apply");
            }

            out(head);
            if (!set.description.isEmpty())
                out(QString("        %1").arg(set.description));
            if (!set.note.isEmpty())
                out(QString("        %1").arg(tr("Note: %1").arg(set.note)));
            if (!set.info.isEmpty())
                out(QString("        %1").arg(tr("Info: %1").arg(set.info)));
        }
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
    errorText.clear();

    if (catalog.entries().isEmpty()) {
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

    catalog.resetStatus();

    UINT32 selectedCount = 0;
    QVector<PatchEntry> & entries = catalog.entries();
    for (int i = 0; i < entries.count(); i++) {
        PatchEntry & entry = entries[i];
        if (!isSelected(entry, options)) {
            entry.status = PATCH_STATUS_SKIPPED;
            continue;
        }
        selectedCount++;
        result = applyPatchEntry(ffsEngine, model, entry, options.allCopies);
        if (result && result != ERR_NOTHING_TO_PATCH) {
            errorText = tr("%1 line %2: %3")
                    .arg(catalog.sourceName()).arg(entry.lineNumber).arg(errorMessage(result));
            return result;
        }
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

void UEFIPatch::printReport(const PatchOptions & options) const
{
    const QVector<PatchEntry> & entries = catalog.entries();
    const QVector<PatchSet> & sets = catalog.sets();

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
