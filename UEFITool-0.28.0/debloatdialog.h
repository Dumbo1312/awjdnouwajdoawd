/* debloatdialog.h

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __DEBLOATDIALOG_H__
#define __DEBLOATDIALOG_H__

#include <QDialog>
#include <QList>
#include <QPersistentModelIndex>
#include <QString>
#include <QStringList>
#include <QVector>

#include "basetypes.h"
#include "firmwarereport.h"

class QLabel;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

// Chooses which modules to remove from the opened image.
//
// Everything it offers was found by scanning the image, so the list is what
// is actually in front of you rather than a catalogue of what some other
// board might have. Nothing is ticked by default, and the dependency findings
// for each module are shown before you can tick it.
class DebloatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DebloatDialog(QWidget *parent = 0);
    ~DebloatDialog();

    bool loadCatalog(const QString & path);
    static QString defaultCatalogPath() { return DebloatCatalog::defaultPath(); }

    QString catalogPath() const { return loadedPath; }
    QString lastError() const { return errorText; }
    const DebloatCatalog & catalog() const { return removables; }

    // Fills the list from a scan of the open image.
    void setScan(const FirmwareReport & report, const QVector<DebloatCandidate> & found);

    // What the user ticked.
    QList<QPersistentModelIndex> selectedModules() const;
    QStringList selectedLabels() const;
    // How many of the ticked ones have a dependency finding against them.
    int selectedRiskyCount() const;

    // Clears every tick. Public so a test can put the dialog back to a known
    // state after exercising the selection logic.
    void selectNoneForTesting() { selectNone(); }

private slots:
    void selectNone();
    void itemChanged(QTreeWidgetItem* item, int column);
    void updateDetail();
    void confirmAndAccept();

private:
    void populate();
    void updateButtons();
    void showCategoryDetail(const DebloatCategory & category);
    void showCandidateDetail(const DebloatCandidate & candidate);
    QList<QTreeWidgetItem*> checkableItems() const;

    DebloatCatalog removables;
    FirmwareReport scanReport;
    QVector<DebloatCandidate> candidates;
    QString loadedPath;
    QString errorText;

    QLabel* headerLabel;
    QLabel* platformLabel;
    QLabel* warningLabel;
    QTreeWidget* tree;
    QTextBrowser* detail;
    QPushButton* removeButton;
};

#endif
