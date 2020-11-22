QT += core gui widgets

CONFIG += c++14

INCLUDEPATH += 3rdparty

SOURCES += \
    src/codeiteminfowidget.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/codemodel.cpp \
    src/codemodelcache.cpp \
    src/codemodeldialog.cpp \
    src/treemapwidget.cpp \
    src/progressbar.cpp \
    src/persistent.cpp \
    src/util.cpp \
    3rdparty/hsluv-c/src/hsluv.c

HEADERS += \
    src/codeiteminfowidget.h \
    src/mainwindow.h \
    src/codemodel.h \
    src/codemodelcache.h \
    src/codemodeldialog.h \
    src/treemapwidget.h \
    src/progressbar.h \
    src/persistent.h \
    src/util.h \
    src/squarify.h \
    3rdparty/hsluv-c/src/hsluv.h

FORMS += \
    src/codemodeldialog.ui
