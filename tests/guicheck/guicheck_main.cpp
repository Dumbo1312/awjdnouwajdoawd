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
    if (args.count() != 4) {
        std::cout << "usage: guicheck <test-image> <presets.txt> <output-directory>" << std::endl;
        return 2;
    }

    const QString imagePath = args.at(1);
    const QString presetPath = args.at(2);
    const QString outDir = args.at(3);
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

    std::cout << std::endl
              << checks - failures << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
