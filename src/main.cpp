#include "mainwindow.h"
#include "codemodel.h"
#include "codemodeldialog.h"

#include <QApplication>
#include <QFileInfo>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    a.setApplicationName("locview");
    a.setApplicationVersion("1.0");
    a.setApplicationDisplayName("LOC View");

    MainWindow mainWindow;
    CodeModelDialog dialog;

    // read folders/files from cmd line
    if (argc > 1) {
        QStringList folders;
        for (int i = 1; i < argc; ++i) {
            QFileInfo fileInfo(argv[i]);
            if (fileInfo.isReadable()) {
                QString path = fileInfo.absoluteFilePath();
                while (path.endsWith('/') || path.endsWith('\\'))
                    path.chop(1);
                folders << path;
            }
        }
        dialog.setFolders(folders);
    }

    QObject::connect(&dialog, &CodeModelDialog::accepted, [&]() {
        mainWindow.setCodeDetails(dialog.folders(), dialog.excluded(), dialog.endings());
        dialog.hide();
        mainWindow.show();
    });

    QObject::connect(&mainWindow, &MainWindow::abort, [&]() {
        dialog.show();
        mainWindow.hide();
    });

    QObject::connect(&dialog, &CodeModelDialog::cancelled, [&]() {
        QCoreApplication::quit();
    });

    dialog.show();
    return a.exec();
}
