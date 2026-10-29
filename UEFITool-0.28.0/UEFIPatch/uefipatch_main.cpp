/* uefipatch_main.cpp

Copyright (c) 2018, LongSoft. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <iostream>
#include <string>

#include "../version.h"
#include "uefipatch.h"

#define DEFAULT_PATCH_FILE "patches.txt"

static void usage()
{
    std::cout
        << "UEFIPatch " PROGRAM_VERSION " - UEFI image file patching utility" << std::endl
        << std::endl
        << "Usage: UEFIPatch <image_file> [patches.txt] [options]" << std::endl
        << "       UEFIPatch <image_file> -p \"FileGuid SectionType Patch\" [options]" << std::endl
        << "       UEFIPatch --list [patches.txt]" << std::endl
        << std::endl
        << "Options:" << std::endl
        << "  -o, --output <file>   write the patched image here" << std::endl
        << "                        (default: <image_file>.patched)" << std::endl
        << "  -p, --patch <spec>    apply one patch given on the command line" << std::endl
        << "                        instead of reading a patch file" << std::endl
        << "  -l, --list            list the patch sets in the patch file and exit" << std::endl
        << "  -s, --set <a,b,...>   apply only these patch sets" << std::endl
        << "  -x, --exclude <a,b>   never apply these patch sets" << std::endl
        << "  -n, --dry-run         do everything except writing the output file" << std::endl
        << "  -v, --verbose         report every patch line as applied or not matched" << std::endl
        << "      --strict          exit non-zero if any selected patch line did not match" << std::endl
        << "      --all-copies      patch every matching section, not just the first one" << std::endl
        << "      --hash            print a digest of the input and the output image" << std::endl
        << "  -f, --force           allow the output to overwrite the input image" << std::endl
        << "  -h, --help            show this text" << std::endl
        << std::endl
        << "Patches are read from " DEFAULT_PATCH_FILE " next to the image, or next to" << std::endl
        << "UEFIPatch itself, unless another patch file is named." << std::endl
        << std::endl
        << "A firmware image this tool produces has not been verified against your" << std::endl
        << "board. Keep a known good dump and a way to recover the flash chip." << std::endl;
}

// patches.txt in the working directory, then next to the executable, so that
// an installed UEFIPatch still finds the patch database it shipped with.
static QString resolveDefaultPatchFile()
{
    if (QFileInfo(DEFAULT_PATCH_FILE).exists())
        return DEFAULT_PATCH_FILE;

    const QString beside = QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(DEFAULT_PATCH_FILE);
    if (QFileInfo(beside).exists())
        return beside;

    return DEFAULT_PATCH_FILE;
}

static QStringList splitList(const QString & value)
{
    QStringList items;
    const QStringList raw = value.split(',');
    for (int i = 0; i < raw.count(); i++) {
        const QString item = raw.at(i).trimmed();
        if (!item.isEmpty())
            items.append(item);
    }
    return items;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setOrganizationName("LongSoft");
    a.setOrganizationDomain("longsoft.me");
    a.setApplicationName("UEFIPatch");

    const QStringList & args = a.arguments();
    const int argumentsCount = args.length();

    QString inputPath;
    QString patches;
    QString outputPath;
    QString argumentPatch;
    bool listOnly = false;
    bool patchFromArgument = false;
    PatchOptions options;

    for (int i = 1; i < argumentsCount; i++) {
        const QString & arg = args.at(i);

        if (arg == "-h" || arg == "--help") {
            usage();
            return ERR_SUCCESS;
        }
        else if (arg == "-l" || arg == "--list") {
            listOnly = true;
        }
        else if (arg == "-n" || arg == "--dry-run") {
            options.dryRun = true;
        }
        else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        }
        else if (arg == "--strict") {
            options.strict = true;
        }
        else if (arg == "--all-copies") {
            options.allCopies = true;
        }
        else if (arg == "--hash") {
            options.hashes = true;
        }
        else if (arg == "-f" || arg == "--force") {
            options.force = true;
        }
        else if (arg == "-p" || arg == "--patch") {
            if (i + 1 >= argumentsCount) {
                std::cout << "Error: " << arg.toLocal8Bit().constData()
                          << " needs a patch specification" << std::endl;
                return ERR_INVALID_PARAMETER;
            }
            patchFromArgument = true;
            argumentPatch = args.at(++i);
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argumentsCount) {
                std::cout << "Error: " << arg.toLocal8Bit().constData()
                          << " needs a file name" << std::endl;
                return ERR_INVALID_PARAMETER;
            }
            outputPath = args.at(++i);
        }
        else if (arg == "-s" || arg == "--set") {
            if (i + 1 >= argumentsCount) {
                std::cout << "Error: " << arg.toLocal8Bit().constData()
                          << " needs a comma separated list of set names" << std::endl;
                return ERR_INVALID_PARAMETER;
            }
            options.includeSets += splitList(args.at(++i));
        }
        else if (arg == "-x" || arg == "--exclude") {
            if (i + 1 >= argumentsCount) {
                std::cout << "Error: " << arg.toLocal8Bit().constData()
                          << " needs a comma separated list of set names" << std::endl;
                return ERR_INVALID_PARAMETER;
            }
            options.excludeSets += splitList(args.at(++i));
        }
        else if (arg.startsWith("-")) {
            std::cout << "Error: unknown option " << arg.toLocal8Bit().constData() << std::endl
                      << "Run UEFIPatch --help for the list of options." << std::endl;
            return ERR_INVALID_PARAMETER;
        }
        // First bare argument is the image, second is the patch file.
        else if (inputPath.isEmpty()) {
            inputPath = arg;
        }
        else if (patches.isEmpty()) {
            patches = arg;
        }
        else {
            std::cout << "Error: unexpected argument " << arg.toLocal8Bit().constData() << std::endl;
            return ERR_INVALID_PARAMETER;
        }
    }

    // --list takes the patch file as its only bare argument.
    if (listOnly && patches.isEmpty() && !inputPath.isEmpty()) {
        patches = inputPath;
        inputPath.clear();
    }

    if (patches.isEmpty())
        patches = resolveDefaultPatchFile();

    if (!listOnly && inputPath.isEmpty()) {
        usage();
        return ERR_SUCCESS;
    }

    if (patchFromArgument && options.strict)
        options.verbose = true;

    UEFIPatch w;
    UINT8 result;

    if (patchFromArgument)
        result = w.loadPatchArgument(argumentPatch);
    else
        result = w.loadPatchFile(patches);

    const QStringList warnings = w.lastWarnings();
    for (int i = 0; i < warnings.count(); i++)
        std::cout << "Warning: " << warnings.at(i).toLocal8Bit().constData() << std::endl;

    if (result == ERR_SUCCESS && listOnly) {
        w.listSets();
        return ERR_SUCCESS;
    }

    if (result == ERR_SUCCESS) {
        if (outputPath.isEmpty())
            outputPath = inputPath + ".patched";
        result = w.run(inputPath, outputPath, options);
    }

    if (result == ERR_SUCCESS)
        return ERR_SUCCESS;

    // A loaded diagnostic always says more than the error code it came with,
    // so it replaces the generic text rather than being printed next to it.
    const QString detail = w.lastError();
    if (!detail.isEmpty()) {
        std::cout << "Error: " << detail.toLocal8Bit().constData() << std::endl;
        return result;
    }

    switch (result) {
    case ERR_NOTHING_TO_PATCH:
        std::cout << "Error: no patch matched this image" << std::endl;
        break;
    case ERR_INVALID_PARAMETER:
        std::cout << "Error: invalid parameter" << std::endl;
        break;
    case ERR_UNKNOWN_PATCH_TYPE:
        std::cout << "Error: unknown patch type" << std::endl;
        break;
    case ERR_PATCH_OFFSET_OUT_OF_BOUNDS:
        std::cout << "Error: patch offset out of bounds" << std::endl;
        break;
    case ERR_INVALID_SYMBOL:
        std::cout << "Error: pattern format mismatch" << std::endl;
        break;
    case ERR_INVALID_FILE:
        std::cout << "Error: patch file not found or unusable" << std::endl;
        break;
    case ERR_FILE_OPEN:
        std::cout << "Error: input file not found" << std::endl;
        break;
    case ERR_FILE_READ:
        std::cout << "Error: input file can't be read" << std::endl;
        break;
    case ERR_FILE_WRITE:
        std::cout << "Error: output file can't be written" << std::endl;
        break;
    default:
        std::cout << "Error: " << errorMessage(result).toLocal8Bit().constData() << std::endl;
    }

    return result;
}
