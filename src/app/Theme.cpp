#include "Theme.h"
#include <QFile>
#include <QPalette>
#include <QColor>

namespace Theme {

void apply(QApplication *app)
{
    QPalette p;
    p.setColor(QPalette::Window, QColor("#0E0E0F"));
    p.setColor(QPalette::WindowText, QColor("#ECECEC"));
    p.setColor(QPalette::Base, QColor("#141416"));
    p.setColor(QPalette::AlternateBase, QColor("#17171A"));
    p.setColor(QPalette::Text, QColor("#ECECEC"));
    p.setColor(QPalette::Button, QColor("#1C1C20"));
    p.setColor(QPalette::ButtonText, QColor("#ECECEC"));
    p.setColor(QPalette::Highlight, QColor("#FFD54A"));
    p.setColor(QPalette::HighlightedText, QColor("#141416"));
    p.setColor(QPalette::ToolTipBase, QColor("#141416"));
    p.setColor(QPalette::ToolTipText, QColor("#ECECEC"));
    p.setColor(QPalette::PlaceholderText, QColor("#6A6A70"));
    app->setPalette(p);

    QFile f(":/theme.qss");
    if (f.open(QFile::ReadOnly)) {
        app->setStyleSheet(QString::fromUtf8(f.readAll()));
    }
}

}
