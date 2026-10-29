# Builds the GUI smoke test. It reuses every UEFITool source except
# uefitool_main.cpp, whose main() is replaced by guicheck_main.cpp.

QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET    = guicheck
TEMPLATE  = app
CONFIG   += console
CONFIG   -= app_bundle

ROOT      = $$PWD/../../UEFITool-0.28.0

INCLUDEPATH += $$ROOT

SOURCES  += guicheck_main.cpp \
 $$ROOT/uefitool.cpp \
 $$ROOT/searchdialog.cpp \
 $$ROOT/presetdialog.cpp \
 $$ROOT/debloatdialog.cpp \
 $$ROOT/firmwarereport.cpp \
 $$ROOT/patchset.cpp \
 $$ROOT/theme.cpp \
 $$ROOT/types.cpp \
 $$ROOT/descriptor.cpp \
 $$ROOT/ffs.cpp \
 $$ROOT/peimage.cpp \
 $$ROOT/ffsengine.cpp \
 $$ROOT/treeitem.cpp \
 $$ROOT/treemodel.cpp \
 $$ROOT/messagelistitem.cpp \
 $$ROOT/guidlineedit.cpp \
 $$ROOT/LZMA/LzmaCompress.c \
 $$ROOT/LZMA/LzmaDecompress.c \
 $$ROOT/LZMA/SDK/C/LzFind.c \
 $$ROOT/LZMA/SDK/C/LzmaDec.c \
 $$ROOT/LZMA/SDK/C/LzmaEnc.c \
 $$ROOT/LZMA/SDK/C/Bra86.c \
 $$ROOT/Tiano/EfiTianoDecompress.c \
 $$ROOT/Tiano/EfiTianoCompress.c \
 $$ROOT/Tiano/EfiTianoCompressLegacy.c

HEADERS  += $$ROOT/uefitool.h \
 $$ROOT/searchdialog.h \
 $$ROOT/presetdialog.h \
 $$ROOT/debloatdialog.h \
 $$ROOT/firmwarereport.h \
 $$ROOT/patchset.h \
 $$ROOT/theme.h \
 $$ROOT/ffsengine.h \
 $$ROOT/treemodel.h

FORMS    += $$ROOT/uefitool.ui \
 $$ROOT/searchdialog.ui

RESOURCES += $$ROOT/uefitool.qrc
