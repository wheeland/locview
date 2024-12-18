#include "mainwindow.h"
#include "persistent.h"
#include "codeutil.h"
#include "hsluv-c/src/hsluv.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QThread>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>

static QColor hv2qcolor(float hue, float value)
{
    double r, g, b;
    hsluv2rgb(hue, 100.0, value, &r, &g, &b);
    return QColor(255 * r, 255 * g, 255 * b);
}

static QHash<QString, QColor> getColorPalette(const FileEndingStats::Stats &endings)
{
    // build hue list
    QVector<float> hues{220.f, 360.f, 60.f, 120.f, 30.f, 180.f, 310.f, 275.f, 80.f};
    while (hues.size() < endings.size())
        hues << 360.f * ((float) rand() / (float)RAND_MAX);
    while (hues.size() > endings.size())
        hues.removeLast();

    QHash<QString, QColor> ret;
    for (int i = 0; i < endings.size(); ++i)
        ret[endings[i].ending] = hv2qcolor(hues[i], 80.f);

    return ret;
}

TreeMapNode nodeForFile(const File *file, const QHash<QString, QColor> &palette)
{
    TreeMapNode ret{};
    ret.label = file->name() + "." + file->ending();
    ret.groupLabel = ret.label;
    ret.color = palette[file->ending()];
    ret.size = file->loc();
    ret.userData = (void*) file;
    return ret;
}

TreeMapNode nodeForDir(
        const Directory *dir, const QStringList &excludeList,
        const QString &removePrefix, const FileEndingStats::DirStats &endingStats,
        const QHash<QString, QColor> &palette)
{
    TreeMapNode ret{};
    ret.label = dir->name();
    ret.groupLabel = dir->fullName();
    ret.size = 0;
    ret.userData = (void*) dir;

    if (!removePrefix.isEmpty() && ret.groupLabel.startsWith(removePrefix))
        ret.groupLabel = ret.groupLabel.mid(removePrefix.size());

    float r = 0.0f, g = 0.0f, b = 0.0f;

    for (const CodeItem *child : dir->children()) {
        if (!excludeList.contains(child->path())) {
            if (child->type() == CodeItem::Type_File) {
                ret.children << nodeForFile((File*) child, palette);
                ret.size += child->loc();
            } else {
                ret.children << nodeForDir((Directory*) child, excludeList, removePrefix, endingStats, palette);
                ret.size += ret.children.last().size;
            }

            r += ret.children.last().color.red() * ret.children.last().size;
            g += ret.children.last().color.green() * ret.children.last().size;
            b += ret.children.last().color.blue() * ret.children.last().size;
        }
    }
    ret.color = QColor(r / ret.size, g / ret.size, b / ret.size);

    return ret;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_modelThread = new QThread(this);
    m_modelThread->start();
    m_modelThread->setObjectName("CodeModel thread");

    const QByteArray cacheData = PersistentData::getCacheData();
    m_model = new CodeModel(cacheData);
    m_model->moveToThread(m_modelThread);
    connect(m_model, &CodeModel::cacheDataChanged, this, &MainWindow::onCacheDataChanged, Qt::QueuedConnection);

    setupWidgets();
    resize(800, 600);

    m_progressBar = new ProgressBar(this);
    connect(m_progressBar, &ProgressBar::abort, this, &MainWindow::onAbort);

    connect(m_treeMap, &TreeMapWidget::nodeSelected, this, [=](void *userData) {
        m_selectedInfo->setCodeItem((CodeItem*) userData);
    });
    connect(m_treeMap, &TreeMapWidget::nodeHovered, this, [=](void *userData) {
        m_hoveredInfo->setCodeItem((CodeItem*) userData);
    });

    connect(m_model.data(), &CodeModel::stateChanged, this, &MainWindow::maybeUpdateTreeMapWidget);
    connect(m_model.data(), &CodeModel::stateChanged, this, &MainWindow::onCodeModelProgress, Qt::DirectConnection);
    connect(m_model.data(), &CodeModel::dirCountChanged, this, &MainWindow::onCodeModelProgress, Qt::DirectConnection);
    connect(m_model.data(), &CodeModel::fileCountChanged, this, &MainWindow::onCodeModelProgress, Qt::DirectConnection);
    connect(m_model.data(), &CodeModel::analyzedFileCountChanged, this, &MainWindow::onCodeModelProgress, Qt::DirectConnection);
}

MainWindow::~MainWindow()
{
    m_model->deleteLater();
    m_modelThread->quit();
    m_modelThread->wait();
    delete m_modelThread;
    Q_ASSERT(m_model.data() == nullptr);
}

void MainWindow::setupWidgets()
{
    //
    // Setup TreeMapWidget
    //
    m_treeMap = new TreeMapWidget();
    connect(m_treeMap, &TreeMapWidget::nodeSelected, this, &MainWindow::onTreeMapNodeSelected, Qt::DirectConnection);
    connect(m_treeMap, &TreeMapWidget::nodeRightClicked, this, &MainWindow::onTreeMapNodeRightClicked, Qt::DirectConnection);

    //
    // Right Pane
    //
    QWidget *verticalLayoutWidget = new QWidget();
    QVBoxLayout *verticalLayout = new QVBoxLayout(verticalLayoutWidget);
    verticalLayout->setContentsMargins(0, 0, 0, 0);

    //
    // TreeMap settings
    //
    QGroupBox *treeMapSettingsGroup = new QGroupBox("TreeMap settings", verticalLayoutWidget);
    verticalLayout->addWidget(treeMapSettingsGroup);

    m_depthLabel = new QLabel(treeMapSettingsGroup);
    m_depthSlider = new QSlider(treeMapSettingsGroup);
    m_depthSlider->setMinimum(1);
    m_depthSlider->setMaximum(20);
    m_depthSlider->setOrientation(Qt::Horizontal);
    m_depthSlider->setTickPosition(QSlider::TicksBelow);
    m_depthSlider->setTickInterval(1);
    m_depthSlider->setValue(20);

    m_sizeLabel = new QLabel(treeMapSettingsGroup);
    m_sizeSlider = new QSlider(treeMapSettingsGroup);
    m_sizeSlider->setMaximum(500);
    m_sizeSlider->setOrientation(Qt::Horizontal);
    m_sizeSlider->setTickInterval(1);

    m_groupLabel = new QLabel(treeMapSettingsGroup);
    m_groupSlider = new QSlider(treeMapSettingsGroup);
    m_groupSlider->setMaximum(500);
    m_groupSlider->setOrientation(Qt::Horizontal);
    m_groupSlider->setTickInterval(1);

    QVBoxLayout *treeMapSettingsGroupLayout = new QVBoxLayout(treeMapSettingsGroup);
    treeMapSettingsGroupLayout->setContentsMargins(0, 0, 0, 0);
    treeMapSettingsGroupLayout->addWidget(m_depthLabel);
    treeMapSettingsGroupLayout->addWidget(m_depthSlider);
    treeMapSettingsGroupLayout->addWidget(m_sizeLabel);
    treeMapSettingsGroupLayout->addWidget(m_sizeSlider);
    treeMapSettingsGroupLayout->addWidget(m_groupLabel);
    treeMapSettingsGroupLayout->addWidget(m_groupSlider);

    //
    // Selected Entity
    //
    m_selectedInfo = new CodeItemInfoWidget(verticalLayoutWidget);
    m_selectedInfo->setTitle("Selected Item");
    m_selectedInfo->setExcludeList(m_excludeList);
    verticalLayout->addWidget(m_selectedInfo);

    //
    // Hovered Entity
    //
    m_hoveredInfo = new CodeItemInfoWidget(verticalLayoutWidget);
    m_hoveredInfo->setTitle("Hovered Item");
    m_hoveredInfo->setExcludeList(m_excludeList);
    verticalLayout->addWidget(m_hoveredInfo);

    verticalLayout->addStretch();

    //
    // Splitter
    //
    QSplitter *splitter = new QSplitter(this);
    splitter->addWidget(m_treeMap);
    splitter->addWidget(verticalLayoutWidget);
    setCentralWidget(splitter);

    m_menubar = new QMenuBar(this);
    m_menubar->setObjectName(QString::fromUtf8("menubar"));
    m_menubar->setGeometry(QRect(0, 0, 800, 22));
    setMenuBar(m_menubar);

    m_statusbar = new QStatusBar(this);
    m_statusbar->setObjectName(QString::fromUtf8("statusbar"));
    setStatusBar(m_statusbar);

    connect(m_depthSlider, &QSlider::valueChanged, this, &MainWindow::updateLabels);
    connect(m_sizeSlider, &QSlider::valueChanged, this, &MainWindow::updateLabels);
    connect(m_groupSlider, &QSlider::valueChanged, this, &MainWindow::updateLabels);

    updateLabels();
}

void MainWindow::onCodeModelProgress()
{
    m_modelStateMutex.lock();

    m_modelState = m_model->state();
    m_modelFiles = m_model->fileCount();
    m_modelDirs = m_model->dirCount();
    m_modelAnalyzed = m_model->analyzedFileCount();

    if (!m_progressBarUpdateScheduled) {
        m_progressBarUpdateScheduled = true;
        QTimer::singleShot(0, this, &MainWindow::updateProgressBar);
    }

    m_modelStateMutex.unlock();
}

void MainWindow::updateProgressBar()
{
    m_modelStateMutex.lock();

    if (m_modelState == CodeModel::State_Done) {
        m_progressBar->ready();
    } else if (m_modelState == CodeModel::State_Enumerating) {
        m_progressBar->enumerating(m_modelDirs, m_modelFiles);
    } else if (m_modelState == CodeModel::State_Analyzing) {
        m_progressBar->analyzing(m_modelAnalyzed, m_modelFiles);
    }
    m_progressBarUpdateScheduled = false;

    m_modelStateMutex.unlock();
}

void MainWindow::onAbort()
{
    m_model->cancelUpdate();
    m_progressBar->ready();
    emit abort();
}

void MainWindow::onCacheDataChanged(const QByteArray &data)
{
    PersistentData::setCacheData(data);
}

void MainWindow::setCodeDetails(QStringList paths, QStringList excluded, QStringList endings)
{
    PersistentData::setIncludePaths(paths);
    PersistentData::setExcludePaths(excluded);
    PersistentData::setFileEndings(endings);

    QTimer::singleShot(0, m_model.data(), [=]() {
        m_model->setFileEndings(endings);
        m_model->setRootDirNames(paths);
        m_model->setExcludePaths(excluded);
        m_model->update();
    });
}

void MainWindow::maybeUpdateTreeMapWidget()
{
    if (m_model->state() != CodeModel::State_Done)
        return;

    // if there is only one root node, we can remove its prefix from all
    // groupLabels along the way
    const QVector<const Directory*> rootDirs =  m_model->rootDirs();
    const QString removePrefix = (rootDirs.size() == 1) ? rootDirs.first()->fullName() : QString();

    // collect File Ending Stats for each Directory and assign file ending colors
    const FileEndingStats::DirStats endingStats = FileEndingStats::getDirStats(rootDirs, m_excludeList);
    const QHash<QString, QColor> palette = getColorPalette(endingStats.total);

    // build root TreeMapNode
    TreeMapNode rootNode{"root", "root", QColor(), 0.0f, {}, nullptr};
    for (const Directory *dir : rootDirs) {
        if (!m_excludeList.contains(dir->path())) {
            rootNode.children << nodeForDir(dir, m_excludeList, removePrefix, endingStats, palette);
            rootNode.size += rootNode.children.last().size;
        }
    }

    m_treeMap->setRootNode(rootNode);
}

void MainWindow::updateLabels()
{
    m_depthLabel->setText(QString("Max Depth: %1").arg(m_depthSlider->value()));
    m_sizeLabel->setText(QString("Max Item Size: %1").arg(m_sizeSlider->value()));
    m_groupLabel->setText(QString("Min Group Size: %1").arg(m_groupSlider->value()));

    m_treeMap->setMaxDepth(m_depthSlider->value());
    m_treeMap->setMaxSize(m_sizeSlider->value());
    m_treeMap->setMinGroupSize(m_groupSlider->value());

    m_depthSlider->setValue(m_treeMap->maxDepth());
    m_sizeSlider->setValue(m_treeMap->maxSize());
    m_groupSlider->setValue(m_treeMap->minGroupSize());
}

void MainWindow::onTreeMapNodeSelected(void *userData)
{
}

void MainWindow::onTreeMapNodeRightClicked(void *userData, QPoint pos)
{
    CodeItem *item = (CodeItem*) userData;
    if (!item)
        return;

    const QString name = ((CodeItem*) userData)->fullName();
    const QString path = ((CodeItem*) userData)->path();
    QMenu contextMenu("Context menu", this);

    const QString openType = (item->type() == CodeItem::Type_Directory) ? "Browse " : "Open ";
    QAction *open = new QAction(openType + name, &contextMenu);
    QObject::connect(open, &QAction::triggered, [=]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    contextMenu.addAction(open);

    QAction *exclude = new QAction("Exclude " + name, &contextMenu);
    QObject::connect(exclude, &QAction::triggered, [=]() {
        excludePath(path);
    });
    contextMenu.addAction(exclude);

    contextMenu.exec(pos);
}

void MainWindow::excludePath(const QString &path)
{
    QTimer::singleShot(0, m_model.data(), [=]() {
        m_model->addExcludePath(path);
        PersistentData::setExcludePaths(m_model->excludePaths());
    });
    m_excludeList << path;
    m_selectedInfo->setExcludeList(m_excludeList);
    m_hoveredInfo->setExcludeList(m_excludeList);
    maybeUpdateTreeMapWidget();
}
