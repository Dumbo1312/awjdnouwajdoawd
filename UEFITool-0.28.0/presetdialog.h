/* presetdialog.h

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __PRESETDIALOG_H__
#define __PRESETDIALOG_H__

#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>

#include "basetypes.h"
#include "firmwarereport.h"
#include "patchset.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

// Picks which patch sets to apply to the open image. The dialog only selects;
// the main window owns the engine and does the applying, so that there is one
// place where an image gets modified.
class PresetDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PresetDialog(QWidget *parent = 0);
    ~PresetDialog();

    // Reads a preset file. On failure returns false and lastError() says why.
    bool loadCatalog(const QString & path);
    // Where the presets that ship with the program live, or an empty string
    // when none of the expected locations has a file.
    static QString defaultCatalogPath();

    QString catalogPath() const { return loadedPath; }
    QString lastError() const { return errorText; }
    const PatchCatalog & catalog() const { return presets; }
    // Applying a patch records its outcome on the entry, so the caller
    // that does the applying needs the catalog mutable.
    PatchCatalog & catalog() { return presets; }

    // What a scan of the open image says each set and each line would do.
    // Without this the dialog still works, it just cannot say which presets
    // are relevant to the image in front of you.
    void setApplicability(const QMap<QString, UINT8> & sets, const QMap<int, UINT8> & lines);

    // Ids of the sets the user ticked, in file order.
    QStringList selectedSets() const;
    // Whether to patch every matching section rather than the first one.
    bool allCopies() const;

private slots:
    void browseForCatalog();
    void selectAll();
    void selectNone();
    void selectSuggested();
    void itemChanged(QTreeWidgetItem* item, int column);
    void updateDetail();

private:
    void populate();
    void updateApplyButton();
    void showDetail(const PatchSet & set);

    PatchCatalog presets;
    QMap<QString, UINT8> setApplies;
    QMap<int, UINT8> lineApplies;
    bool haveApplicability;
    QString loadedPath;
    QString errorText;

    QLabel* headerLabel;
    QLabel* sourceLabel;
    QTreeWidget* tree;
    QTextBrowser* detail;
    QCheckBox* allCopiesBox;
    QPushButton* suggestedButton;
    QPushButton* applyButton;
};

#endif
