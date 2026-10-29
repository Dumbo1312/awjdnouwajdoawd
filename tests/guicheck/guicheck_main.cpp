/* guicheck_main.cpp

A headless smoke test for the UEFITool GUI additions.

It links the same sources as UEFITool but replaces main(), so it can build the
main window and the preset dialog under the "offscreen" platform plugin, check
the things that are easy to break, and save screenshots of the theme. No X
server and no real firmware are needed.

Usage: guicheck <test-image> <presets.txt> <output-directory>

Exit code is 0 when every check passed, 1 otherwise. Failures are printed one
per line so the calling script can show them.

*/

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <iostream>

#include "debloatdialog.h"
#include "firmwarereport.h"
#include "presetdialog.h"
#include "theme.h"
#include "uefitool.h"

static int failures = 0;
static int checks = 0;

static void check(bool condition, const QString & what)
{
    checks++;
    if (condition) {
        std::cout << "ok    " << what.toLocal8Bit().constData() << std::endl;
    }
    else {
        failures++;
        std::cout << "FAIL  " << what.toLocal8Bit().constData() << std::endl;
    }
}

// Lets the event loop settle so that layouts are final before a grab.
static void settle()
{
    for (int i = 0; i < 8; i++)
        qApp->processEvents();
}

static bool saveShot(QWidget* widget, const QString & path)
{
    settle();
    const QPixmap shot = widget->grab();
    if (shot.isNull())
        return false;
    return shot.save(path, "PNG");
}

// Finds a menu by the text of its own action, with the & accelerator removed.
static QMenu* findMenu(QMenuBar* bar, const QString & title)
{
    const QList<QAction*> actions = bar->actions();
    for (int i = 0; i < actions.count(); i++) {
        QAction* action = actions.at(i);
        if (!action->menu())
            continue;
        if (QString(action->text()).remove('&') == title)
            return action->menu();
    }
    return NULL;
}

static QAction* findAction(QMenu* menu, const QString & title)
{
    if (!menu)
        return NULL;
    const QList<QAction*> actions = menu->actions();
    for (int i = 0; i < actions.count(); i++) {
        if (QString(actions.at(i)->text()).remove('&') == title)
            return actions.at(i);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("CodeRush");
    a.setOrganizationDomain("coderush.me");
    a.setApplicationName("UEFIToolGuiCheck");

    const QStringList args = a.arguments();
    if (args.count() != 5) {
        std::cout << "usage: guicheck <test-image> <presets.txt> <debloat.txt> "
                     "<output-directory>" << std::endl;
        return 2;
    }

    const QString imagePath = args.at(1);
    const QString presetPath = args.at(2);
    const QString debloatPath = args.at(3);
    const QString outDir = args.at(4);
    QDir().mkpath(outDir);

    // ------------------------------------------------------------- theme ---

    const QString sheet = Theme::styleSheet();
    check(!sheet.isEmpty(), "the theme stylesheet is in the compiled in resource");
    check(sheet.contains(Theme::accentColor()),
          "the stylesheet uses the accent colour the code reports");

    Theme::apply(true);
    check(!a.styleSheet().isEmpty(), "applying the theme sets an application stylesheet");
    Theme::apply(false);
    check(a.styleSheet().isEmpty(), "turning the theme off clears it");
    Theme::apply(true);

    // ------------------------------------------------------ preset dialog ---

    PresetDialog dialog;
    const bool loaded = dialog.loadCatalog(presetPath);
    check(loaded, "the preset dialog loads the preset file");
    if (!loaded)
        std::cout << "        " << dialog.lastError().toLocal8Bit().constData() << std::endl;

    const PatchCatalog & catalog = dialog.catalog();
    const QStringList platforms = catalog.platforms();
    check(platforms.contains("Intel"), "the presets are grouped by platform, Intel among them");
    check(platforms.contains("AMD"), "the presets are grouped by platform, AMD among them");
    check(platforms.contains("Graphics"), "the presets are grouped by platform, Graphics among them");

    const PatchSet* intel = catalog.setById("intel-cstate-unlock");
    check(intel != NULL, "the Intel C-state preset is in the catalog");
    check(intel && intel->isApplicable(), "the Intel C-state preset has patch lines to apply");
    check(intel && intel->risk == "medium", "the Intel C-state preset carries its risk");

    const PatchSet* amd = catalog.setById("amd-no-patterns");
    check(amd != NULL, "the AMD entry is in the catalog");
    check(amd && !amd->isApplicable(), "the AMD entry has nothing to apply");
    check(amd && !amd->info.isEmpty(), "the AMD entry explains itself instead");

    QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
    check(tree != NULL, "the dialog has a preset tree");

    if (tree) {
        check(tree->topLevelItemCount() == platforms.count(),
              "the tree shows one group per platform");

        // Count the items the user can actually tick.
        int checkable = 0;
        QTreeWidgetItem* intelItem = NULL;
        for (int g = 0; g < tree->topLevelItemCount(); g++) {
            QTreeWidgetItem* group = tree->topLevelItem(g);
            for (int c = 0; c < group->childCount(); c++) {
                QTreeWidgetItem* item = group->child(c);
                if (item->flags() & Qt::ItemIsUserCheckable) {
                    checkable++;
                    if (!intelItem && item->toolTip(0) == "intel-cstate-unlock")
                        intelItem = item;
                }
            }
        }
        check(checkable == 4, "exactly the four applicable presets are tickable");
        check(intelItem != NULL, "the Intel C-state preset has a tickable row");

        check(dialog.selectedSets().isEmpty(), "nothing is selected to begin with");

        if (intelItem) {
            intelItem->setCheckState(0, Qt::Checked);
            settle();
            check(dialog.selectedSets() == QStringList("intel-cstate-unlock"),
                  "ticking a preset reports it as selected");

            // Select the row as well, so the detail pane fills in for the shot.
            tree->setCurrentItem(intelItem);
            settle();
        }
    }

    check(dialog.allCopies(), "patching every copy of a module is the default");

    dialog.resize(900, 620);
    dialog.show();
    check(saveShot(&dialog, outDir + "/offscreen-preset-dialog.png"), "the preset dialog renders to a PNG");
    dialog.hide();

    // --------------------------------------------------------- main window ---

    UEFITool window;
    window.resize(1180, 740);
    window.show();
    settle();

    QMenuBar* bar = window.menuBar();
    QMenu* tweaks = findMenu(bar, "Tweaks");
    QMenu* view = findMenu(bar, "View");
    check(tweaks != NULL, "the main window has a Tweaks menu");
    check(view != NULL, "the main window has a View menu");

    QAction* presetsAction = findAction(tweaks, "Performance presets...");
    check(presetsAction != NULL, "Tweaks holds the performance presets action");
    check(presetsAction && !presetsAction->isEnabled(),
          "presets are disabled until an image is open");

    QAction* themeAction = findAction(view, "Yellow on black theme");
    check(themeAction != NULL, "View holds the theme toggle");
    check(themeAction && themeAction->isCheckable(), "the theme toggle is checkable");

    // Help must stay the last menu, so the new ones went in before it.
    const QList<QAction*> barActions = bar->actions();
    check(!barActions.isEmpty()
          && QString(barActions.last()->text()).remove('&') == "Help",
          "Help is still the last menu");

    window.openImageFile(imagePath);
    settle();
    check(presetsAction && presetsAction->isEnabled(),
          "opening an image enables the presets action");

    check(saveShot(&window, outDir + "/offscreen-main-window.png"), "the main window renders to a PNG");

    // The theme toggle must take effect without restarting.
    if (themeAction) {
        themeAction->setChecked(false);
        settle();
        check(a.styleSheet().isEmpty(), "unticking the theme toggle clears the stylesheet");
        themeAction->setChecked(true);
        settle();
        check(!a.styleSheet().isEmpty(), "ticking it again restores the theme");
    }

    // ------------------------------------------------- the apply path ---
    // UEFITool::applyPresets() is private and ends in modal dialogs, so the
    // sequence it performs is run here directly. The image it produces is
    // written out so the calling script can diff it against the one UEFIPatch
    // makes from the same preset: the GUI and the command line tool share
    // applyPatchEntry(), and this is what proves they stay in step.
    {
        QFile input(imagePath);
        if (!input.open(QFile::ReadOnly)) {
            check(false, "the test image can be read");
        }
        else {
            const QByteArray buffer = input.readAll();
            input.close();

            FfsEngine engine;
            check(engine.parseImageFile(buffer) == ERR_SUCCESS,
                  "the engine parses the test image");

            PatchCatalog applyCatalog;
            check(applyCatalog.loadFile(presetPath) == ERR_SUCCESS,
                  "the apply path loads the same preset file");

            UINT32 applied = 0;
            UINT32 sections = 0;
            QVector<PatchEntry> & entries = applyCatalog.entries();
            for (int i = 0; i < entries.count(); i++) {
                PatchEntry & entry = entries[i];
                if (entry.setId != "intel-cstate-unlock") {
                    entry.status = PATCH_STATUS_SKIPPED;
                    continue;
                }
                const UINT8 result = applyPatchEntry(&engine, engine.treeModel(), entry, true);
                check(result == ERR_SUCCESS || result == ERR_NOTHING_TO_PATCH,
                      QString("patch line %1 applies without error").arg(entry.lineNumber));
                if (entry.status == PATCH_STATUS_APPLIED) {
                    applied++;
                    sections += entry.sectionsPatched;
                }
            }

            check(applied > 0, "the Intel preset matches something in the test image");
            check(sections >= applied, "every applied line changed at least one section");

            QByteArray rebuilt;
            check(engine.reconstructImageFile(rebuilt) == ERR_SUCCESS,
                  "the tweaked image reconstructs");
            check(rebuilt != buffer, "the rebuilt image differs from the input");
            check(rebuilt.size() == buffer.size(),
                  "the rebuilt image is the same size as the input");

            QFile output(outDir + "/gui-applied.bin");
            if (output.open(QFile::WriteOnly)) {
                output.write(rebuilt);
                output.close();
                check(true, "the tweaked image is written out for comparison");
            }
            else {
                check(false, "the tweaked image is written out for comparison");
            }
        }
    }

    // ------------------------------------------------------ image scan ---
    // The scanner is what lets both dialogs talk about the image in front of
    // you rather than about firmware in general, so it gets checked against
    // a fixture whose contents are known exactly.
    {
        QFile input(imagePath);
        if (!input.open(QFile::ReadOnly)) {
            check(false, "the test image can be read for scanning");
        }
        else {
            const QByteArray buffer = input.readAll();
            input.close();

            FfsEngine engine;
            check(engine.parseImageFile(buffer) == ERR_SUCCESS,
                  "the engine parses the fixture for scanning");

            const FirmwareScanner scanner(&engine, engine.treeModel());
            const FirmwareReport report = scanner.scan();

            check(report.platform == FW_PLATFORM_INTEL,
                  "the fixture is recognised as an Intel image");
            check(!report.platformEvidence.isEmpty(),
                  "the platform verdict comes with evidence");
            check(report.modules.count() >= 12,
                  "every module in the fixture is inventoried");
            check(report.namedModuleCount >= 11,
                  "modules are identified by the name they carry themselves");
            check(report.volumeCount >= 1, "the volume is counted");
            check(!report.aprioriGuids.isEmpty(),
                  "the DXE apriori list is parsed");

            // Preset applicability. The fixture carries the Haswell and
            // Skylake patterns and nothing else, so the Intel C-state preset
            // must come back as applicable and the HEDT one must not.
            PatchCatalog presetCatalog;
            check(presetCatalog.loadFile(presetPath) == ERR_SUCCESS,
                  "the scanner can load the preset catalogue");
            const QMap<QString, UINT8> applies = scanner.evaluatePresets(presetCatalog);

            check(applies.value("intel-cstate-unlock") == FW_MATCH_APPLIES,
                  "the Intel C-state preset is reported as applicable");
            check(applies.value("intel-hedt-power-unlock") != FW_MATCH_APPLIES,
                  "the HEDT preset is not reported as applicable");
            check(applies.value("pointer-protocol-fix") == FW_MATCH_ABSENT,
                  "a preset whose module is absent is reported as absent");

            // Evaluating must not change anything: it is a read only pass.
            QByteArray afterScan;
            check(engine.reconstructImageFile(afterScan) == ERR_SUCCESS,
                  "the image still reconstructs after evaluation");
            check(afterScan == buffer,
                  "evaluating presets leaves the image untouched");

            // Debloat candidates.
            DebloatCatalog debloat;
            const UINT8 loadResult = debloat.loadFile(debloatPath);
            check(loadResult == ERR_SUCCESS, "the debloat catalogue loads");
            if (loadResult != ERR_SUCCESS)
                std::cout << "        " << debloat.error().toLocal8Bit().constData() << std::endl;
            check(debloat.categories().count() >= 10,
                  "the debloat catalogue has its categories");

            // Boot critical categories must be marked protected, which is
            // what stops the dialog ever putting a checkbox on them.
            const DebloatCategory* csm = debloat.categoryById("csm-legacy");
            check(csm != NULL && csm->isProtected,
                  "the CSM category is protected");
            const DebloatCategory* storage = debloat.categoryById("unused-storage");
            check(storage != NULL && storage->isProtected,
                  "the storage controller category is protected");
            const DebloatCategory* flash = debloat.categoryById("flash-path");
            check(flash != NULL && flash->isProtected,
                  "the flash write path category is protected");
            const DebloatCategory* network = debloat.categoryById("network-boot");
            check(network != NULL && !network->isProtected,
                  "the network boot category is not protected");
            check(debloat.entries().count() >= 90,
                  "the debloat catalogue has its entries");

            const QVector<DebloatCandidate> candidates =
                    scanner.findCandidates(debloat, report);
            check(candidates.count() >= 6,
                  "the fixture's removable modules are found");

            bool foundCsm = false;
            bool foundLogo = false;
            bool foundPxe = false;
            for (int i = 0; i < candidates.count(); i++) {
                const DebloatCandidate & candidate = candidates.at(i);
                if (candidate.module.name == "CsmDxe") {
                    foundCsm = true;
                    check(candidate.inApriori,
                          "a module in the apriori list is flagged as such");
                }
                else if (candidate.module.name == "LogoDxe") {
                    foundLogo = true;
                    check(candidate.referencedBy.contains("BootManagerDxe"),
                          "a module referenced by GUID names the module referencing it");
                    check(candidate.looksRisky(),
                          "a referenced module counts as risky");
                }
                else if (candidate.module.name == "UefiPxeBcDxe") {
                    foundPxe = true;
                    check(candidate.categoryId == "network-boot",
                          "a candidate lands in the right category");
                    check(!candidate.looksRisky(),
                          "a module nothing points at is not flagged");
                }
            }
            check(foundCsm, "CsmDxe is offered for removal");
            check(foundLogo, "LogoDxe is offered for removal");
            check(foundPxe, "UefiPxeBcDxe is offered for removal");

            // The apriori file itself must never be offered for removal.
            bool offeredApriori = false;
            for (int i = 0; i < candidates.count(); i++) {
                if (candidates.at(i).module.name.contains("apriori", Qt::CaseInsensitive))
                    offeredApriori = true;
            }
            check(!offeredApriori, "the apriori file itself is not offered for removal");
        }
    }

    // ------------------------------------------------- the debloat dialog ---
    {
        DebloatDialog debloatUi;
        const bool loaded = debloatUi.loadCatalog(debloatPath);
        check(loaded, "the debloat dialog loads its catalogue");

        QFile input(imagePath);
        if (loaded && input.open(QFile::ReadOnly)) {
            const QByteArray buffer = input.readAll();
            input.close();

            FfsEngine engine;
            engine.parseImageFile(buffer);
            const FirmwareScanner scanner(&engine, engine.treeModel());
            const FirmwareReport report = scanner.scan();
            debloatUi.setScan(report, scanner.findCandidates(debloatUi.catalog(), report));

            check(debloatUi.selectedModules().isEmpty(),
                  "the debloat dialog starts with nothing selected");

            QTreeWidget* debloatTree = debloatUi.findChild<QTreeWidget*>();
            check(debloatTree != NULL, "the debloat dialog has a tree");
            if (debloatTree) {
                check(debloatTree->topLevelItemCount() >= 3,
                      "only categories with something found are shown");

                int rows = 0;
                bool sawCsm = false;
                bool sawPxe = false;
                bool headingTickable = false;
                for (int g = 0; g < debloatTree->topLevelItemCount(); g++) {
                    QTreeWidgetItem* group = debloatTree->topLevelItem(g);
                    if (group->flags() & Qt::ItemIsUserCheckable)
                        headingTickable = true;
                    for (int c = 0; c < group->childCount(); c++) {
                        QTreeWidgetItem* item = group->child(c);
                        rows++;
                        const QString name = item->text(0);
                        const bool tickable = (item->flags() & Qt::ItemIsUserCheckable) != 0;
                        if (name == "CsmDxe") {
                            sawCsm = true;
                            check(!tickable, "CsmDxe is listed but has no checkbox");
                        }
                        else if (name == "UefiPxeBcDxe") {
                            sawPxe = true;
                            check(tickable, "a module in an unprotected category is tickable");
                        }
                    }
                }
                check(!headingTickable, "no category heading is tickable on its own");
                check(rows >= 6, "every candidate has a row");
                check(sawCsm, "a protected module is still shown");
                check(sawPxe, "an unprotected module is still shown");

                // Ticking everything that can be ticked must never select a
                // protected module, whatever the user does in the dialog.
                for (int g = 0; g < debloatTree->topLevelItemCount(); g++) {
                    QTreeWidgetItem* group = debloatTree->topLevelItem(g);
                    for (int c = 0; c < group->childCount(); c++) {
                        QTreeWidgetItem* item = group->child(c);
                        if (item->flags() & Qt::ItemIsUserCheckable)
                            item->setCheckState(0, Qt::Checked);
                    }
                }
                settle();
                const QStringList picked = debloatUi.selectedLabels();
                check(!picked.isEmpty(), "ticking everything selects something");
                check(!picked.contains("CsmDxe"),
                      "selecting everything still cannot select a protected module");
            }

            // Back to a clean dialog for the screenshot.
            debloatUi.selectNoneForTesting();
            debloatUi.resize(1000, 720);
            debloatUi.show();
            check(saveShot(&debloatUi, outDir + "/offscreen-debloat-dialog.png"),
                  "the debloat dialog renders to a PNG");
            debloatUi.hide();
        }
    }

    std::cout << std::endl
              << checks - failures << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
