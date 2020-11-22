#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QPointer>
#include <QMutex>

#include "treemapwidget.h"
#include "codemodel.h"
#include "codeiteminfowidget.h"
#include "progressbar.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setCodeDetails(QStringList paths, QStringList excluded, QStringList endings);

    TreeMapWidget *m_treeMap;

private slots:
    void maybeUpdateTreeMapWidget();
    void updateLabels();
    void onTreeMapNodeSelected(void *userData);
    void onTreeMapNodeRightClicked(void *userData, QPoint pos);
    void onCodeModelProgress();
    void updateProgressBar();
    void onAbort();
    void onCacheDataChanged(const QByteArray &data);

signals:
    void abort();

private:
    void setupWidgets();
    QLabel *m_depthLabel;
    QSlider *m_depthSlider;
    QLabel *m_sizeLabel;
    QSlider *m_sizeSlider;
    QLabel *m_groupLabel;
    QSlider *m_groupSlider;
    QMenuBar *m_menubar;
    QStatusBar *m_statusbar;

    CodeItemInfoWidget *m_selectedInfo;
    CodeItemInfoWidget *m_hoveredInfo;

    QThread *m_modelThread;
    QPointer<CodeModel> m_model;

    QStringList m_excludeList;

    QMutex m_modelStateMutex;
    ProgressBar *m_progressBar;
    bool m_progressBarUpdateScheduled = false;
    CodeModel::State m_modelState;
    int m_modelFiles;
    int m_modelDirs;
    int m_modelAnalyzed;

    void excludePath(const QString &path);
};
