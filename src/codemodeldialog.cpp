#include "codemodeldialog.h"
#include "ui_codemodeldialog.h"
#include "persistent.h"

#include <QStringListModel>
#include <QFileDialog>
#include <QDebug>

CodeModelDialog::CodeModelDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CodeModelDialog)
{
    ui->setupUi(this);
    ui->endingsButtonLayout->addStretch();
    ui->folderButtonLayout->addStretch();
    ui->excludedButtonLayout->addStretch();

    m_folderModel = new QStringListModel(PersistentData::getIncludePaths());
    m_excludedModel = new QStringListModel(PersistentData::getExcludePaths());
    m_endingsModel = new QStringListModel(PersistentData::getFileEndings());

    connect(ui->acceptButton, &QPushButton::clicked, this, &CodeModelDialog::accepted);
    connect(ui->cancelButton, &QPushButton::clicked, this, &CodeModelDialog::cancelled);

    //
    // Setup Folder View
    //
    connect(ui->folderAddButton, &QPushButton::clicked, this, [=]() {
        QStringList folders = m_folderModel->stringList();
        folders << QFileDialog::getExistingDirectory();
        m_folderModel->setStringList(folders);
    });

    connect(ui->folderRemoveButton, &QPushButton::clicked, this, [=]() {
        const QModelIndexList indices = ui->folderList->selectionModel()->selectedRows();
        if (indices.size() == 1)
            m_folderModel->removeRows(indices.first().row(), 1);
    });

    connect(ui->folderList, &QAbstractItemView::doubleClicked, this, [=](QModelIndex idx) {
        QStringList folders = m_folderModel->stringList();
        folders[idx.row()] = QFileDialog::getExistingDirectory(nullptr, QString(), folders[idx.row()]);
        m_folderModel->setStringList(folders);
    });

    connect(ui->folderEditButton, &QPushButton::clicked, this, [=]() {
        const QModelIndexList indices = ui->folderList->selectionModel()->selectedRows();
        if (indices.size() == 1) {
            QStringList folders = m_folderModel->stringList();
            folders[indices.first().row()] = QFileDialog::getExistingDirectory(nullptr, QString(), folders[indices.first().row()]);
            m_folderModel->setStringList(folders);
        }
    });

    connect(ui->folderClearButton, &QPushButton::clicked, this, [=]() {
        m_folderModel->setStringList(QStringList());
    });

    ui->folderList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->folderList->setMovement(QListView::Free);
    ui->folderList->setDragDropMode(QAbstractItemView::InternalMove);
    ui->folderList->setModel(m_folderModel);
    ui->folderList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    //
    // Setup Excluded View
    //
    connect(ui->excludedAddButton, &QPushButton::clicked, this, [=]() {
        QStringList excludeds = m_excludedModel->stringList();
        excludeds << QFileDialog::getExistingDirectory();
        m_excludedModel->setStringList(excludeds);
    });

    connect(ui->excludedRemoveButton, &QPushButton::clicked, this, [=]() {
        const QModelIndexList indices = ui->excludedList->selectionModel()->selectedRows();
        if (indices.size() == 1)
            m_excludedModel->removeRows(indices.first().row(), 1);
    });

    connect(ui->excludedList, &QAbstractItemView::doubleClicked, this, [=](QModelIndex idx) {
        QStringList excludeds = m_excludedModel->stringList();
        excludeds[idx.row()] = QFileDialog::getExistingDirectory(nullptr, QString(), excludeds[idx.row()]);
        m_excludedModel->setStringList(excludeds);
    });

    connect(ui->excludedEditButton, &QPushButton::clicked, this, [=]() {
        const QModelIndexList indices = ui->excludedList->selectionModel()->selectedRows();
        if (indices.size() == 1) {
            QStringList excludeds = m_excludedModel->stringList();
            excludeds[indices.first().row()] = QFileDialog::getExistingDirectory(nullptr, QString(), excludeds[indices.first().row()]);
            m_excludedModel->setStringList(excludeds);
        }
    });

    connect(ui->excludedClearButton, &QPushButton::clicked, this, [=]() {
        m_excludedModel->setStringList(QStringList());
    });

    ui->excludedList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->excludedList->setMovement(QListView::Free);
    ui->excludedList->setDragDropMode(QAbstractItemView::InternalMove);
    ui->excludedList->setModel(m_excludedModel);
    ui->excludedList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    //
    // Setup File Endings View
    //
    connect(ui->endingsAddButton, &QPushButton::clicked, this, [=]() {
        m_endingsModel->setStringList(m_endingsModel->stringList() << "asdf");
        const QModelIndex idx = m_endingsModel->index(m_endingsModel->rowCount() - 1);
        ui->endingsList->selectionModel()->clearSelection();
        ui->endingsList->selectionModel()->select(idx, QItemSelectionModel::Select);
        ui->endingsList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Current);
        ui->endingsList->edit(idx);
    });

    connect(ui->endingsRemoveButton, &QPushButton::clicked, this, [=]() {
        const QModelIndexList indices = ui->endingsList->selectionModel()->selectedRows();
        if (indices.size() == 1)
            m_endingsModel->removeRows(indices.first().row(), 1);
    });

    connect(ui->endingsEditButton, &QPushButton::clicked, this, [=]() {
        const QModelIndexList indices = ui->endingsList->selectionModel()->selectedRows();
        if (indices.size() == 1)
            ui->endingsList->edit(indices.first());
    });

    connect(ui->endingsClearButton, &QPushButton::clicked, this, [=]() {
        m_endingsModel->setStringList(QStringList());
    });

    ui->endingsList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->endingsList->setMovement(QListView::Free);
    ui->endingsList->setDragDropMode(QAbstractItemView::NoDragDrop);
    ui->endingsList->setModel(m_endingsModel);
}

CodeModelDialog::~CodeModelDialog()
{
    delete ui;
}

void CodeModelDialog::clear()
{
    m_endingsModel->setStringList({});
    m_folderModel->setStringList({});
}

void CodeModelDialog::setFolders(const QStringList &f)
{
    m_folderModel->setStringList(f);
}

void CodeModelDialog::setExcluded(const QStringList &f)
{
    m_excludedModel->setStringList(f);
}

void CodeModelDialog::setEndings(const QStringList &f)
{
    m_endingsModel->setStringList(f);
}

void CodeModelDialog::resizeEvent(QResizeEvent * /*event*/)
{
    ui->verticalLayoutWidget->resize(width() - 20, height() - 20);
}

