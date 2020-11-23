#include "codemodel.h"
#include "persistent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QThread>

class CodeModelAnalyzerThread : public QThread
{
public:
    CodeModelAnalyzerThread(const QVector<File*> &files, std::atomic<int> &counter, std::atomic<int> &abortFlag)
        : m_files(files), m_counter(counter), m_abortFlag(abortFlag) {}

    void run() override
    {
        while (m_abortFlag.load() == 0) {
            int nextIdx = m_counter.fetch_add(1);
            if (nextIdx >= m_files.size())
                return;

            File *file = m_files[nextIdx];
            QFile f(file->path());

            if (f.open(QFile::ReadOnly)) {
                const QByteArray data = f.readAll();
                int loc = 1;
                for (char c : data) {
                    if (c == '\n')
                        loc++;
                }

                file->m_ok = true;
                file->m_loc = loc;
            } else {
                file->m_ok = false;
                file->m_loc = 0;
            }
        }
    }

private:
    const QVector<File*> &m_files;
    std::atomic<int> &m_counter;
    std::atomic<int> &m_abortFlag;
};

Directory::Directory(const QString &name, const QString &path, Directory *parent)
    : m_name(name)
    , m_path(path)
    , m_parent(parent)
{
    if (parent) {
        m_fullName = parent->fullName() + m_name + QDir::separator();
    } else {
        m_fullName = m_name + QDir::separator();
    }
}

Directory::~Directory()
{
}

void Directory::traverse(const ConstFileVisitor &visitor) const
{
    for (const CodeItem *child : m_children)
        child->traverse(visitor);
}

void Directory::traverse(const ConstDirectoryVisitor &visitor, TraversalType traversalType) const
{
    if (traversalType == ItemFirst)
        visitor(this);
    for (const CodeItem *child : m_children)
        child->traverse(visitor, traversalType);
    if (traversalType == ChildrenFirst)
        visitor(this);
}

void Directory::traverse(const FileVisitor &visitor)
{
    for (CodeItem *child : m_children)
        child->traverse(visitor);
}

void Directory::traverse(const DirectoryVisitor &visitor, TraversalType traversalType)
{
    if (traversalType == ItemFirst)
        visitor(this);
    for (CodeItem *child : m_children)
        child->traverse(visitor, traversalType);
    if (traversalType == ChildrenFirst)
        visitor(this);
}

QString File::path() const
{
    return m_dir->path() + QDir::separator() + m_name + "." + m_ending;
}

QString File::fullName() const
{
    return m_dir->fullName() + m_name + "." + m_ending;
}

void File::traverse(const ConstFileVisitor &visitor) const
{
    visitor(this);
}

void File::traverse(const ConstDirectoryVisitor &/*visitor*/, TraversalType /*traversalType*/) const
{
}

void File::traverse(const FileVisitor &visitor)
{
    visitor(this);
}

void File::traverse(const DirectoryVisitor &/*visitor*/, TraversalType /*traversalType*/)
{
}

File::File(Directory *dir, const QString &name, const QString &ending, qint64 sz, const QDateTime &lastModified)
    : m_dir(dir)
    , m_name(name)
    , m_ending(ending)
    , m_size(sz)
    , m_lastModified(lastModified)
{
}

File::~File()
{
}

CodeModel::CodeModel(const QByteArray &cacheData, QObject *parent)
    : QObject(parent)
    , m_abortFlag(0)
{
    setState(State_Done);
    m_cache.deserialize(cacheData);
}

CodeModel::~CodeModel()
{
    clear();
}

void CodeModel::setState(CodeModel::State state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void CodeModel::setDirCount(int dirCount)
{
    m_dirCount = dirCount;
    emit dirCountChanged();
}

void CodeModel::setFileCount(int fileCount)
{
    m_fileCount = fileCount;
    fileCountChanged();
}

void CodeModel::setAnalyzedFileCount(int analzedFileCount)
{
    m_analyzedFileCount = analzedFileCount;
    analyzedFileCountChanged();
}

void CodeModel::setFileEndings(const QStringList &fileEndings)
{
    m_fileEndings = fileEndings;
}

void CodeModel::setRootDirNames(const QStringList &rootDirNames)
{
    m_rootDirNames = rootDirNames;
}

void CodeModel::setExcludePaths(const QStringList &excludePaths)
{
    m_excludePaths = excludePaths;
    m_excludeAbsolutePaths = excludePaths;

    for (QString &abs : m_excludeAbsolutePaths) {
        abs = QFileInfo(abs).absoluteFilePath();
    }
}

void CodeModel::addExcludePath(const QString &path)
{
    QStringList paths = m_excludePaths;
    paths << path;
    setExcludePaths(paths);
}

void CodeModel::removeExcludePath(const QString &path)
{
    QStringList paths = m_excludePaths;
    paths.removeAll(path);
    setExcludePaths(paths);
}

void CodeModel::update()
{
    m_abortFlag.store(0);
    clear();
    recompute();
}

QVector<const Directory*> CodeModel::rootDirs() const
{
    QVector<const Directory*> ret;
    for (Directory *dir : m_rootDirs)
        ret << dir;
    return ret;
}

void CodeModel::cancelUpdate()
{
    m_abortFlag.store(1);
}

void CodeModel::clear()
{
    for (Directory *dir : m_rootDirs)
        delete dir;
    m_rootDirs.clear();
    setFileCount(0);
    setDirCount(0);
    setAnalyzedFileCount(0);
    setState(State_Empty);
}

void CodeModel::recompute()
{
    setState(State_Enumerating);

    // remove stale root dirs
    for (auto it = m_rootDirs.begin(); it != m_rootDirs.end(); /*empty*/) {
        if (m_rootDirNames.contains(it.key())) {
            ++it;
        } else {
            delete *it;
            it = m_rootDirs.erase(it);
        }
    }

    // re-count files/dirs
    m_fileCount = 0;
    m_analyzedFileCount = 0;
    m_dirCount = 0;
    for (Directory *dir : m_rootDirs) {
        dir->traverse([&](const Directory*) { m_dirCount++; }, CodeItem::ItemFirst);
        dir->traverse([&](const File*) { m_fileCount++; m_analyzedFileCount++; });
    }

    // emit changed signals
    setFileCount(m_fileCount);
    setDirCount(m_dirCount);
    setAnalyzedFileCount(m_analyzedFileCount);

    // add new root dirs
    for (const QString &rootDirName : m_rootDirNames) {
        if (!m_rootDirs[rootDirName]) {
            QFileInfo dir(rootDirName);
            m_rootDirs[rootDirName] = new Directory(dir.fileName(), rootDirName, nullptr);
        }
        enumerate(m_rootDirs[rootDirName]);
    }

    setState(State_Analyzing);

    // enumerate all files that are not yet cached
    QVector<File*> files;
    for (Directory *rootDir : m_rootDirs) {
        rootDir->traverse([&](File *file) {
            int loc;
            if (m_cache.getEntry(file->path(), file->size(), file->lastModified(), loc)) {
                file->m_ok = true;
                file->m_loc = loc;
                setAnalyzedFileCount(m_analyzedFileCount + 1);
            } else {
                files << file;
            }
        });
    }

    // spawn a bunch of threads to analyze the files
    std::atomic<int> idx(0);
    QVector<CodeModelAnalyzerThread*> threads;
    static int threadCount = PersistentData::getCodeModelThreadCount();
    for (int i = 0; i < threadCount; ++i) {
        threads << new CodeModelAnalyzerThread(files, idx, m_abortFlag);
        threads.last()->start();
    }
    while (m_abortFlag.load() == 0 && idx.load() < files.size()) {
        setAnalyzedFileCount(idx.load());
        QThread::usleep(100* 1000);
    }
    for (CodeModelAnalyzerThread *thread : threads) {
        thread->wait();
        delete thread;
    }

    // Write results to cache
    for (File *file : files) {
        if (file->m_ok) {
            m_cache.saveEntry(file->path(), file->size(), file->lastModified(), file->loc());
        }
    }

    // Accumulate file locs for parent dirs
    for (Directory *rootDir : m_rootDirs) {
        rootDir->traverse([&](Directory *dir) {
            dir->m_loc = std::accumulate(dir->m_children.begin(), dir->m_children.end(), 0, [](int n, CodeItem *item) {
                return n + item->loc() + n;
            });
        }, CodeItem::ChildrenFirst);
    }

    setState(State_Done);

    // If abort flag was raised, clear everything, so we don't end up with partial state
    if (m_abortFlag.load() != 0) {
        clear();
    }

    emit cacheDataChanged(m_cache.serialize());
}

void CodeModel::enumerate(Directory *dir)
{
    const QDir dirInfo(dir->path());
    auto flags = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks;
    const QFileInfoList files = dirInfo.entryInfoList(flags);

    for (const QFileInfo &file : files) {
        const QString abs = file.absoluteFilePath();
        if (m_excludeAbsolutePaths.contains(abs))
            continue;

        // Abort early if flag is raised
        if (m_abortFlag.load() != 0) {
            return;
        }

        if (file.isDir()) {
            Directory *subdir = new Directory(file.fileName(), abs, dir);
            enumerate(subdir);

            if (!subdir->m_children.empty()) {
                dir->m_children << subdir;
                setDirCount(m_dirCount + 1);
            } else {
                delete subdir;
            }
        }
        else if (file.isFile() && m_fileEndings.contains(file.suffix(), Qt::CaseInsensitive)) {
            dir->m_children << new File(dir, file.completeBaseName(), file.suffix(), file.size(), file.lastModified());
            setFileCount(m_fileCount + 1);
        }
    }

    // move dirs in front of files
    std::sort(dir->m_children.begin(), dir->m_children.end(), [](CodeItem *a, CodeItem *b) {
        if (a->type() == b->type())
            return (qintptr) a < (qintptr) b;
        return (int) a->type() < (int) b->type();
    });
}
