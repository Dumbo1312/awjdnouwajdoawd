/* debloatdialog.cpp

Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "debloatdialog.h"
#include "theme.h"

#define COLUMN_NAME     0
#define COLUMN_SIZE     1
#define COLUMN_RISK     2
#define COLUMN_DEPENDS  3

#define ROLE_CANDIDATE  (Qt::UserRole + 1)
#define ROLE_CATEGORY   (Qt::UserRole + 2)

static QString humanSize(quint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QObject::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QObject::tr("%1 B").arg(bytes);
}

DebloatDialog::DebloatDialog(QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Debloat"));
    resize(900, 680);

    headerLabel = new QLabel(tr("Remove modules from the open image"), this);
    headerLabel->setProperty("accent", true);
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);

    platformLabel = new QLabel(this);
    platformLabel->setProperty("dim", true);
    platformLabel->setWordWrap(false);

    warningLabel = new QLabel(
        tr("This does not increase frame rate. Nothing here will give you a single extra FPS: "
           "by the time the operating system is running, none of this code is executing. "
           "What it buys is a shorter POST and less code before the OS. What it can cost is a "
           "machine that does not boot."), this);
    warningLabel->setProperty("warning", true);
    warningLabel->setWordWrap(true);

    tree = new QTreeWidget(this);
    tree->setColumnCount(4);
    QStringList headers;
    headers << tr("Module") << tr("Size") << tr("Risk") << tr("Dependencies");
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->header()->setStretchLastSection(false);
#if QT_VERSION >= 0x050000
    tree->header()->setSectionResizeMode(COLUMN_NAME, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(COLUMN_SIZE, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(COLUMN_RISK, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(COLUMN_DEPENDS, QHeaderView::ResizeToContents);
#else
    tree->header()->setResizeMode(COLUMN_NAME, QHeaderView::Stretch);
    tree->header()->setResizeMode(COLUMN_SIZE, QHeaderView::ResizeToContents);
    tree->header()->setResizeMode(COLUMN_RISK, QHeaderView::ResizeToContents);
    tree->header()->setResizeMode(COLUMN_DEPENDS, QHeaderView::ResizeToContents);
#endif

    detail = new QTextBrowser(this);
    detail->setMinimumHeight(150);

    QPushButton* noneButton = new QPushButton(tr("Select none"), this);
    removeButton = new QPushButton(tr("Remove selected"), this);
    removeButton->setDefault(true);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(noneButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addWidget(cancelButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(headerLabel);
    layout->addWidget(platformLabel);
    layout->addWidget(warningLabel);
    layout->addSpacing(4);
    layout->addWidget(tree, 3);
    layout->addWidget(detail, 1);
    layout->addLayout(buttonLayout);

    connect(noneButton, SIGNAL(clicked()), this, SLOT(selectNone()));
    connect(removeButton, SIGNAL(clicked()), this, SLOT(confirmAndAccept()));
    connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
    connect(tree, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
            this, SLOT(itemChanged(QTreeWidgetItem*, int)));
    connect(tree, SIGNAL(itemSelectionChanged()), this, SLOT(updateDetail()));

    updateButtons();
}

DebloatDialog::~DebloatDialog()
{
}

bool DebloatDialog::loadCatalog(const QString & path)
{
    errorText.clear();

    if (path.isEmpty()) {
        errorText = tr("No debloat catalogue was found. It ships as knowledge/debloat.txt.");
        return false;
    }

    if (removables.loadFile(path)) {
        errorText = removables.error();
        return false;
    }

    loadedPath = path;
    return true;
}

void DebloatDialog::setScan(const FirmwareReport & report, const QVector<DebloatCandidate> & found)
{
    scanReport = report;
    candidates = found;
    populate();
}

void DebloatDialog::populate()
{
    tree->blockSignals(true);
    tree->clear();

    QString platformText = tr("Platform: %1").arg(scanReport.platformName);
    if (!scanReport.vendor.isEmpty())
        platformText += tr("   Firmware: %1").arg(scanReport.vendor);
    platformText += tr("   %1 module(s) scanned, %2 removable found")
            .arg(scanReport.modules.count()).arg(candidates.count());
    platformLabel->setText(platformText);
    platformLabel->setToolTip(scanReport.summaryLines().join("\n"));

    const QVector<DebloatCategory> & categories = removables.categories();

    for (int c = 0; c < categories.count(); c++) {
        const DebloatCategory & category = categories.at(c);

        // Only categories with something found in this image.
        QVector<int> matched;
        for (int i = 0; i < candidates.count(); i++) {
            if (candidates.at(i).categoryId == category.id)
                matched.append(i);
        }
        if (matched.isEmpty())
            continue;

        const bool platformMatches =
                category.platform.isEmpty()
                || category.platform.compare(PATCH_PLATFORM_ANY, Qt::CaseInsensitive) == 0
                || scanReport.platform == FW_PLATFORM_UNKNOWN
                || category.platform.compare(scanReport.platformName, Qt::CaseInsensitive) == 0;

        QTreeWidgetItem* group = new QTreeWidgetItem(tree);
        group->setText(COLUMN_NAME, category.label.isEmpty() ? category.id : category.label);
        group->setData(COLUMN_NAME, ROLE_CATEGORY, category.id);
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QFont groupFont = group->font(COLUMN_NAME);
        groupFont.setBold(true);
        group->setFont(COLUMN_NAME, groupFont);
        group->setForeground(COLUMN_NAME, QColor(Theme::accentColor()));
        group->setText(COLUMN_RISK, category.risk.isEmpty() ? tr("-") : category.risk);
        if (category.isProtected) {
            group->setText(COLUMN_DEPENDS, tr("protected"));
            group->setForeground(COLUMN_DEPENDS, QColor(Theme::warningColor()));
        }

        quint64 categoryBytes = 0;

        for (int m = 0; m < matched.count(); m++) {
            const int candidateIndex = matched.at(m);
            const DebloatCandidate & candidate = candidates.at(candidateIndex);
            categoryBytes += candidate.module.size;

            QTreeWidgetItem* item = new QTreeWidgetItem(group);
            const QString label = candidate.module.name.isEmpty()
                    ? candidate.module.guid : candidate.module.name;
            item->setText(COLUMN_NAME, label);
            item->setToolTip(COLUMN_NAME, tr("%1\nGUID %2\n%3")
                             .arg(candidate.description).arg(candidate.module.guid)
                             .arg(candidate.module.volume));
            item->setData(COLUMN_NAME, ROLE_CANDIDATE, candidateIndex);
            item->setText(COLUMN_SIZE, humanSize(candidate.module.size));

            if (category.isProtected) {
                // Shown so you know it is in your image, with no way to tick
                // it. Boot critical things do not belong behind a checkbox.
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
            else {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                               | Qt::ItemIsUserCheckable);
                item->setCheckState(COLUMN_NAME, Qt::Unchecked);
            }

            QString risk = category.risk.isEmpty() ? tr("-") : category.risk;
            if (candidate.inApriori || candidate.isLastOfKind)
                risk = tr("high");
            item->setText(COLUMN_RISK, risk);

            QString depends;
            if (category.isProtected)
                depends = tr("protected");
            else if (candidate.inApriori)
                depends = tr("in apriori list");
            else if (!candidate.referencedBy.isEmpty())
                depends = tr("%1 reference(s)").arg(candidate.referencedBy.count());
            else
                depends = tr("none found");
            item->setText(COLUMN_DEPENDS, depends);
            if (category.isProtected) {
                item->setForeground(COLUMN_NAME, QColor(Theme::dimTextColor()));
                item->setForeground(COLUMN_DEPENDS, QColor(Theme::warningColor()));
            }

            if (candidate.looksRisky()) {
                item->setForeground(COLUMN_DEPENDS, QColor(Theme::warningColor()));
                item->setForeground(COLUMN_RISK, QColor(Theme::warningColor()));
            }
            if (!platformMatches)
                item->setForeground(COLUMN_NAME, QColor(Theme::dimTextColor()));
        }

        group->setText(COLUMN_SIZE, humanSize(categoryBytes));
        if (!platformMatches) {
            group->setText(COLUMN_DEPENDS, tr("other platform"));
            group->setForeground(COLUMN_DEPENDS, QColor(Theme::dimTextColor()));
        }
    }

    tree->expandAll();
    tree->blockSignals(false);

    if (tree->topLevelItemCount() == 0) {
        detail->setHtml(tr("<p>Nothing in the catalogue matches this image.</p>"
                           "<p>That is a normal result. Identification is by the name each "
                           "module carries in its own user interface section, and plenty of "
                           "firmware ships with those stripped out, which leaves only GUIDs "
                           "to go on.</p>"));
    }
    else {
        detail->setHtml(tr("<p>Select a row to read what it is, what removing it gains you, "
                           "and what the dependency scan found.</p>"
                           "<p>Modules shown in <b>%1</b> under Dependencies have something "
                           "pointing at them. That does not always mean removal breaks the "
                           "machine, but it does mean you need a reason to believe it will "
                           "not.</p>").arg(tr("amber")));
    }

    updateButtons();
}

void DebloatDialog::selectNone()
{
    tree->blockSignals(true);
    const QList<QTreeWidgetItem*> items = checkableItems();
    for (int i = 0; i < items.count(); i++)
        items.at(i)->setCheckState(COLUMN_NAME, Qt::Unchecked);
    tree->blockSignals(false);
    updateButtons();
}

QList<QTreeWidgetItem*> DebloatDialog::checkableItems() const
{
    QList<QTreeWidgetItem*> items;
    for (int g = 0; g < tree->topLevelItemCount(); g++) {
        QTreeWidgetItem* group = tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); c++) {
            QTreeWidgetItem* item = group->child(c);
            if (item->flags() & Qt::ItemIsUserCheckable)
                items.append(item);
        }
    }
    return items;
}

void DebloatDialog::itemChanged(QTreeWidgetItem* item, int column)
{
    if (column != COLUMN_NAME || !item)
        return;
    updateButtons();
}

void DebloatDialog::updateDetail()
{
    const QList<QTreeWidgetItem*> selection = tree->selectedItems();
    if (selection.isEmpty())
        return;

    QTreeWidgetItem* item = selection.first();

    const QVariant candidateData = item->data(COLUMN_NAME, ROLE_CANDIDATE);
    if (candidateData.isValid()) {
        const int index = candidateData.toInt();
        if (index >= 0 && index < candidates.count())
            showCandidateDetail(candidates.at(index));
        return;
    }

    const QString categoryId = item->data(COLUMN_NAME, ROLE_CATEGORY).toString();
    const DebloatCategory* category = removables.categoryById(categoryId);
    if (category)
        showCategoryDetail(*category);
}

void DebloatDialog::showCategoryDetail(const DebloatCategory & category)
{
    QString html;
    html += tr("<p><b>%1</b><br><span style=\"color:%2\">%3</span></p>")
            .arg(category.label.isEmpty() ? category.id : category.label)
            .arg(Theme::dimTextColor()).arg(category.id);

    if (category.isProtected) {
        html += tr("<p style=\"color:%1\"><b>Protected: this tool will not remove these.</b></p>")
                .arg(Theme::warningColor());
    }
    if (!category.risk.isEmpty())
        html += tr("<p>Risk: <b>%1</b></p>").arg(category.risk);
    if (!category.impact.isEmpty())
        html += tr("<p><b>What removing this gains you</b><br>%1</p>")
                .arg(category.impact.join(" "));
    if (!category.requirements.isEmpty())
        html += tr("<p><b>Only if</b><br>%1</p>").arg(category.requirements.join(" "));
    for (int i = 0; i < category.notes.count(); i++)
        html += tr("<p>%1</p>").arg(category.notes.at(i));

    detail->setHtml(html);
}

void DebloatDialog::showCandidateDetail(const DebloatCandidate & candidate)
{
    const DebloatCategory* category = removables.categoryById(candidate.categoryId);
    QString html;
    html += tr("<p><b>%1</b><br><span style=\"color:%2\">%3</span></p>")
            .arg(candidate.module.name.isEmpty() ? candidate.module.guid : candidate.module.name)
            .arg(Theme::dimTextColor()).arg(candidate.module.guid);

    if (!candidate.description.isEmpty())
        html += tr("<p>%1</p>").arg(candidate.description);

    html += tr("<p>%1 in %2</p>")
            .arg(humanSize(candidate.module.size)).arg(candidate.module.volume);

    if (category && category->isProtected) {
        html += tr("<p style=\"color:%1\"><b>Protected.</b> This module is listed so you can "
                   "see that it is in your image, but it has no checkbox and this tool will "
                   "not remove it. If you genuinely need it gone, do it by hand in the tree "
                   "and accept that you are on your own.</p>").arg(Theme::warningColor());
    }

    html += tr("<p><b>Dependency scan</b></p>");
    if (candidate.inApriori) {
        html += tr("<p style=\"color:%1\">Listed in %2. The firmware dispatcher is told to run "
                   "this module at a specific point, which is a strong sign something expects "
                   "it to exist. Removing it without also fixing the apriori list is how a "
                   "board stops booting.</p>")
                .arg(Theme::warningColor()).arg(candidate.aprioriName);
    }
    if (!candidate.referencedBy.isEmpty()) {
        html += tr("<p style=\"color:%1\">This module's GUID appears in the body of: %2.</p>")
                .arg(Theme::warningColor()).arg(candidate.referencedBy.join(", "));
        html += tr("<p>Something looks this module up by file GUID. That is usually a real "
                   "dependency, though a GUID can also turn up in a table of modules the "
                   "firmware merely knows about.</p>");
    }
    if (candidate.isLastOfKind) {
        html += tr("<p style=\"color:%1\">This is the only module of its file type in the "
                   "image.</p>").arg(Theme::warningColor());
    }
    if (!candidate.looksRisky() && !candidate.isLastOfKind) {
        html += tr("<p>Nothing found: no apriori entry, and no other module carries this "
                   "GUID.</p>"
                   "<p>This is the weakest kind of good news. A dependency expression records "
                   "protocol GUIDs rather than file GUIDs, so a module can be required without "
                   "its file GUID appearing anywhere. A clean scan means nothing was found "
                   "against it, not that it is safe to remove.</p>");
    }

    if (category && !category->notes.isEmpty())
        html += tr("<p><b>About this category</b><br>%1</p>").arg(category->notes.join(" "));

    detail->setHtml(html);
}

void DebloatDialog::updateButtons()
{
    removeButton->setEnabled(!selectedModules().isEmpty());
}

QList<QPersistentModelIndex> DebloatDialog::selectedModules() const
{
    QList<QPersistentModelIndex> modules;
    const QList<QTreeWidgetItem*> items = checkableItems();
    for (int i = 0; i < items.count(); i++) {
        if (items.at(i)->checkState(COLUMN_NAME) != Qt::Checked)
            continue;
        const int index = items.at(i)->data(COLUMN_NAME, ROLE_CANDIDATE).toInt();
        if (index >= 0 && index < candidates.count())
            modules.append(candidates.at(index).module.index);
    }
    return modules;
}

QStringList DebloatDialog::selectedLabels() const
{
    QStringList labels;
    const QList<QTreeWidgetItem*> items = checkableItems();
    for (int i = 0; i < items.count(); i++) {
        if (items.at(i)->checkState(COLUMN_NAME) != Qt::Checked)
            continue;
        const int index = items.at(i)->data(COLUMN_NAME, ROLE_CANDIDATE).toInt();
        if (index >= 0 && index < candidates.count()) {
            const DebloatCandidate & candidate = candidates.at(index);
            labels.append(candidate.module.name.isEmpty()
                          ? candidate.module.guid : candidate.module.name);
        }
    }
    return labels;
}

int DebloatDialog::selectedRiskyCount() const
{
    int risky = 0;
    const QList<QTreeWidgetItem*> items = checkableItems();
    for (int i = 0; i < items.count(); i++) {
        if (items.at(i)->checkState(COLUMN_NAME) != Qt::Checked)
            continue;
        const int index = items.at(i)->data(COLUMN_NAME, ROLE_CANDIDATE).toInt();
        if (index >= 0 && index < candidates.count()
                && candidates.at(index).looksRisky())
            risky++;
    }
    return risky;
}

void DebloatDialog::confirmAndAccept()
{
    const QStringList labels = selectedLabels();
    if (labels.isEmpty())
        return;

    const int risky = selectedRiskyCount();

    QString text = tr("Remove %1 module(s) from the image?").arg(labels.count());
    if (risky > 0) {
        text += tr("\n\n%1 of them have something pointing at them: an apriori entry, or "
                   "another module carrying their GUID. Those are the ones most likely to "
                   "stop the machine booting.").arg(risky);
    }
    text += tr("\n\nNothing is written to disk by this step. The image in the window is "
               "changed, and you still have to save it.");

    QMessageBox box(this);
    box.setWindowTitle(tr("Confirm removal"));
    box.setIcon(risky > 0 ? QMessageBox::Warning : QMessageBox::Question);
    box.setText(text);
    box.setDetailedText(labels.join("\n"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);

    if (box.exec() == QMessageBox::Yes)
        accept();
}
