/* presetdialog.cpp

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "presetdialog.h"
#include "theme.h"

#define PRESET_FILE_NAME "presets.txt"

// Column layout of the preset tree
#define COLUMN_NAME    0
#define COLUMN_RISK    1
#define COLUMN_PATCHES 2

// Role holding the set id on a child item
#define ROLE_SET_ID (Qt::UserRole + 1)

PresetDialog::PresetDialog(QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Performance presets"));
    resize(760, 560);

    headerLabel = new QLabel(tr("Pick what to change in the open image"), this);
    headerLabel->setProperty("accent", true);
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);

    sourceLabel = new QLabel(this);
    sourceLabel->setProperty("dim", true);
    sourceLabel->setWordWrap(true);

    tree = new QTreeWidget(this);
    tree->setColumnCount(3);
    QStringList headers;
    headers << tr("Preset") << tr("Risk") << tr("Patch lines");
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->header()->setStretchLastSection(false);
    // The preset names are long sentences, so the name column takes whatever
    // is left rather than forcing the risk and count columns off the edge.
#if QT_VERSION >= 0x050000
    tree->header()->setSectionResizeMode(COLUMN_NAME, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(COLUMN_RISK, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(COLUMN_PATCHES, QHeaderView::ResizeToContents);
#else
    tree->header()->setResizeMode(COLUMN_NAME, QHeaderView::Stretch);
    tree->header()->setResizeMode(COLUMN_RISK, QHeaderView::ResizeToContents);
    tree->header()->setResizeMode(COLUMN_PATCHES, QHeaderView::ResizeToContents);
#endif

    detail = new QTextBrowser(this);
    detail->setMinimumHeight(130);
    detail->setOpenExternalLinks(false);

    allCopiesBox = new QCheckBox(tr("Patch every copy of a module, not just the first"), this);
    allCopiesBox->setChecked(true);
    allCopiesBox->setToolTip(tr("A firmware image often carries the same module in more than one "
                                "volume, for example a recovery copy. With this on, every copy is "
                                "patched; with it off, only the first one found, which is what "
                                "UEFIPatch has always done."));

    QPushButton* loadButton = new QPushButton(tr("Load preset file..."), this);
    QPushButton* allButton = new QPushButton(tr("All"), this);
    QPushButton* noneButton = new QPushButton(tr("None"), this);
    applyButton = new QPushButton(tr("Apply tweaks"), this);
    applyButton->setDefault(true);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(loadButton);
    buttonLayout->addWidget(allButton);
    buttonLayout->addWidget(noneButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(headerLabel);
    layout->addWidget(sourceLabel);
    layout->addSpacing(4);
    layout->addWidget(tree, 3);
    layout->addWidget(detail, 1);
    layout->addWidget(allCopiesBox);
    layout->addLayout(buttonLayout);

    connect(loadButton, SIGNAL(clicked()), this, SLOT(browseForCatalog()));
    connect(allButton, SIGNAL(clicked()), this, SLOT(selectAll()));
    connect(noneButton, SIGNAL(clicked()), this, SLOT(selectNone()));
    connect(applyButton, SIGNAL(clicked()), this, SLOT(accept()));
    connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
    connect(tree, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
            this, SLOT(itemChanged(QTreeWidgetItem*, int)));
    connect(tree, SIGNAL(itemSelectionChanged()), this, SLOT(updateDetail()));

    updateApplyButton();
}

PresetDialog::~PresetDialog()
{
}

QString PresetDialog::defaultCatalogPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    candidates << QDir(appDir).absoluteFilePath(PRESET_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("patches/" PRESET_FILE_NAME);
    // Running straight out of a build directory, with the repository above it.
    candidates << QDir(appDir).absoluteFilePath("../patches/" PRESET_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("../../patches/" PRESET_FILE_NAME);
    candidates << QDir(appDir).absoluteFilePath("../../../patches/" PRESET_FILE_NAME);
    candidates << QString("patches/" PRESET_FILE_NAME);
    candidates << QString(PRESET_FILE_NAME);

    for (int i = 0; i < candidates.count(); i++) {
        const QFileInfo info(candidates.at(i));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }

    return QString();
}

bool PresetDialog::loadCatalog(const QString & path)
{
    errorText.clear();

    if (path.isEmpty()) {
        errorText = tr("No preset file was found. Use \"Load preset file...\" to point at one, "
                       "for example patches/presets.txt from this project.");
        return false;
    }

    const UINT8 result = presets.loadFile(path);
    if (result) {
        errorText = presets.error();
        return false;
    }

    loadedPath = path;
    populate();
    return true;
}

void PresetDialog::populate()
{
    tree->blockSignals(true);
    tree->clear();

    const QStringList platforms = presets.platforms();
    const QVector<PatchSet> & sets = presets.sets();

    UINT32 applicableSets = 0;

    for (int p = 0; p < platforms.count(); p++) {
        const QString & platform = platforms.at(p);

        QTreeWidgetItem* group = new QTreeWidgetItem(tree);
        group->setText(COLUMN_NAME, platform);
        group->setFirstColumnSpanned(false);
        group->setFlags(Qt::ItemIsEnabled);
        QFont groupFont = group->font(COLUMN_NAME);
        groupFont.setBold(true);
        group->setFont(COLUMN_NAME, groupFont);
        group->setForeground(COLUMN_NAME, QColor(Theme::accentColor()));

        for (int i = 0; i < sets.count(); i++) {
            const PatchSet & set = sets.at(i);
            if (set.platform != platform)
                continue;
            if (!set.isApplicable() && set.info.isEmpty())
                continue;

            QTreeWidgetItem* item = new QTreeWidgetItem(group);
            item->setText(COLUMN_NAME, set.description.isEmpty() ? set.id : set.description);
            item->setData(COLUMN_NAME, ROLE_SET_ID, set.id);
            item->setToolTip(COLUMN_NAME, set.id);

            if (set.isApplicable()) {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                item->setCheckState(COLUMN_NAME, Qt::Unchecked);
                item->setText(COLUMN_RISK, set.risk.isEmpty() ? tr("-") : set.risk);
                item->setText(COLUMN_PATCHES, QString::number(set.entries.count()));
                applicableSets++;
            }
            else {
                // Nothing to click: the set exists to explain why there is no
                // patch for this platform, and what to do instead.
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                item->setText(COLUMN_RISK, tr("note"));
                item->setText(COLUMN_PATCHES, tr("-"));
                item->setForeground(COLUMN_NAME, QColor(Theme::dimTextColor()));
                item->setForeground(COLUMN_RISK, QColor(Theme::dimTextColor()));
            }
        }

        // A platform heading with nothing under it is just noise.
        if (group->childCount() == 0)
            delete group;
    }

    tree->expandAll();
    tree->blockSignals(false);

    sourceLabel->setText(tr("%1 - %2 preset(s) that can be applied")
                         .arg(loadedPath).arg(applicableSets));

    detail->setHtml(tr("<p>Tick one or more presets, then <b>Apply tweaks</b>. Each preset is a "
                       "group of byte patterns; a pattern is applied only where it actually "
                       "matches the open image, and you get a report of what matched.</p>"
                       "<p>Entries marked <i>note</i> have nothing to apply. Select one to read "
                       "why.</p>"));

    updateApplyButton();
}

void PresetDialog::browseForCatalog()
{
    const QString startDir = loadedPath.isEmpty() ? QString() : QFileInfo(loadedPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open preset file"), startDir,
                                                      tr("Preset files (*.txt);;All files (*)"));
    if (path.isEmpty())
        return;

    if (!loadCatalog(path)) {
        detail->setHtml(tr("<p><b>%1</b></p>").arg(lastError()));
        return;
    }

    const QStringList complaints = presets.warnings();
    if (!complaints.isEmpty())
        detail->setHtml(tr("<p><b>Warnings</b></p><p>%1</p>").arg(complaints.join("<br>")));
}

void PresetDialog::selectAll()
{
    tree->blockSignals(true);
    for (int g = 0; g < tree->topLevelItemCount(); g++) {
        QTreeWidgetItem* group = tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); c++) {
            QTreeWidgetItem* item = group->child(c);
            if (item->flags() & Qt::ItemIsUserCheckable)
                item->setCheckState(COLUMN_NAME, Qt::Checked);
        }
    }
    tree->blockSignals(false);
    updateApplyButton();
}

void PresetDialog::selectNone()
{
    tree->blockSignals(true);
    for (int g = 0; g < tree->topLevelItemCount(); g++) {
        QTreeWidgetItem* group = tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); c++) {
            QTreeWidgetItem* item = group->child(c);
            if (item->flags() & Qt::ItemIsUserCheckable)
                item->setCheckState(COLUMN_NAME, Qt::Unchecked);
        }
    }
    tree->blockSignals(false);
    updateApplyButton();
}

void PresetDialog::itemChanged(QTreeWidgetItem* item, int column)
{
    if (column != COLUMN_NAME || !item)
        return;
    updateApplyButton();
}

void PresetDialog::updateDetail()
{
    const QList<QTreeWidgetItem*> selection = tree->selectedItems();
    if (selection.isEmpty())
        return;

    const QString id = selection.first()->data(COLUMN_NAME, ROLE_SET_ID).toString();
    if (id.isEmpty())
        return;

    const PatchSet* set = presets.setById(id);
    if (set)
        showDetail(*set);
}

void PresetDialog::showDetail(const PatchSet & set)
{
    QString html;
    html += tr("<p><b>%1</b><br><span style=\"color:%2\">%3</span></p>")
            .arg(set.description.isEmpty() ? set.id : set.description)
            .arg(Theme::dimTextColor())
            .arg(set.id);

    if (set.isApplicable()) {
        html += tr("<p>%1 patch line(s)%2</p>")
                .arg(set.entries.count())
                .arg(set.risk.isEmpty() ? QString() : tr(", risk: <b>%1</b>").arg(set.risk));
    }
    else {
        html += tr("<p><i>Nothing to apply from this entry.</i></p>");
    }

    if (!set.note.isEmpty())
        html += tr("<p><b>Note:</b> %1</p>").arg(set.note);
    if (!set.info.isEmpty())
        html += tr("<p>%1</p>").arg(set.info);

    if (set.isApplicable()) {
        html += tr("<p><b>Modules this preset touches</b></p><ul>");
        const QVector<PatchEntry> & entries = presets.entries();
        QStringList seen;
        for (int i = 0; i < set.entries.count(); i++) {
            const PatchEntry & entry = entries.at(set.entries.at(i));
            const QString line = entry.label.isEmpty() ? entry.guidText.toUpper() : entry.label;
            if (seen.contains(line))
                continue;
            seen.append(line);
            html += tr("<li>%1</li>").arg(line);
        }
        html += tr("</ul>");
    }

    detail->setHtml(html);
}

void PresetDialog::updateApplyButton()
{
    applyButton->setEnabled(!selectedSets().isEmpty());
}

QStringList PresetDialog::selectedSets() const
{
    QStringList ids;
    for (int g = 0; g < tree->topLevelItemCount(); g++) {
        QTreeWidgetItem* group = tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); c++) {
            QTreeWidgetItem* item = group->child(c);
            if (!(item->flags() & Qt::ItemIsUserCheckable))
                continue;
            if (item->checkState(COLUMN_NAME) != Qt::Checked)
                continue;
            const QString id = item->data(COLUMN_NAME, ROLE_SET_ID).toString();
            if (!id.isEmpty() && !ids.contains(id))
                ids.append(id);
        }
    }
    return ids;
}

bool PresetDialog::allCopies() const
{
    return allCopiesBox->isChecked();
}
