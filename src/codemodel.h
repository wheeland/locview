#pragma once

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QVector>
#include <functional>
#include <atomic>

#include "codemodelcache.h"

class File;
class Directory;

using FileVisitor = std::function<void(File*)>;
using DirectoryVisitor = std::function<void(Directory*)>;
using ConstFileVisitor = std::function<void(const File*)>;
using ConstDirectoryVisitor = std::function<void(const Directory*)>;

class CodeItem
{
public:
    enum Type { Type_Directory, Type_File };
    enum TraversalType { ItemFirst, ChildrenFirst };

    virtual Type type() const = 0;
    virtual QString path() const = 0;
    virtual QString name() const = 0;
    virtual QString fullName() const = 0;
    int loc() const { return m_loc; }
    virtual ~CodeItem() {}

    virtual void traverse(const ConstFileVisitor &visitor) const = 0;
    virtual void traverse(const ConstDirectoryVisitor &visitor, TraversalType traversalType) const = 0;
    virtual void traverse(const FileVisitor &visitor) = 0;
    virtual void traverse(const DirectoryVisitor &visitor, TraversalType traversalType) = 0;

protected:
    int m_loc = 0;
};

class Directory : public CodeItem
{
public:
    Type type() const override { return Type_Directory; }

    QString name() const override { return m_name; }
    QString fullName() const override { return m_fullName; }
    QString path() const override { return m_path; }
    Directory *parentDir() const { return m_parent; }

    const QVector<CodeItem*> &children() const { return m_children; }

    void traverse(const ConstFileVisitor &visitor) const override;
    void traverse(const ConstDirectoryVisitor &visitor, TraversalType traversalType) const override;
    void traverse(const FileVisitor &visitor) override;
    void traverse(const DirectoryVisitor &visitor, TraversalType traversalType) override;

private:
    friend class CodeModel;

    Directory(const QString &name, const QString &path, Directory *parent);
    ~Directory();

    void updateLoc();
    void purgeExcludedItems(const QStringList &exclusionList);

    QString m_name;
    QString m_fullName;
    QString m_path;
    Directory *m_parent = nullptr;

    QVector<CodeItem*> m_children;
};

class File : public CodeItem
{
public:
    Type type() const override { return Type_File; }

    Directory *dir() const { return m_dir; }
    QString path() const override;
    QString name() const override { return m_name; }
    QString fullName() const override;
    QString ending() const { return m_ending; }
    qint64 size() const { return m_size; }
    QDateTime lastModified() const { return m_lastModified; }

    bool ok() const { return m_ok; }

    void traverse(const ConstFileVisitor &visitor) const override;
    void traverse(const ConstDirectoryVisitor &visitor, TraversalType traversalType) const override;
    void traverse(const FileVisitor &visitor) override;
    void traverse(const DirectoryVisitor &visitor, TraversalType traversalType) override;

private:
    friend class CodeModel;
    friend class CodeModelAnalyzerThread;
    friend class Directory;

    File(Directory *dir, const QString &name, const QString &ending, qint64 sz, const QDateTime &lastModified);
    ~File();

    Directory *m_dir = nullptr;
    QString m_name;
    QString m_ending;
    bool m_ok = false;
    qint64 m_size;
    QDateTime m_lastModified;
};

class CodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QStringList fileEndings READ fileEndings WRITE setFileEndings)
    Q_PROPERTY(QStringList rootDirNames READ rootDirNames WRITE setRootDirNames)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(int analyzedFileCount READ analyzedFileCount NOTIFY analyzedFileCountChanged)
    Q_PROPERTY(int dirCount READ dirCount NOTIFY dirCountChanged)

public:
    CodeModel(const QByteArray &cacheData, QObject *parent = nullptr);
    ~CodeModel();

    enum State
    {
        State_Empty,
        State_Enumerating,
        State_Analyzing,
        State_Done
    };
    State state() const { return m_state; }

    void setFileEndings(const QStringList &fileEndings);
    QStringList fileEndings() const { return m_fileEndings; }

    void setRootDirNames(const QStringList &rootDirNames);
    QStringList rootDirNames() const { return m_rootDirNames; }

    void setExcludePaths(const QStringList &excludePaths);
    void addExcludePath(const QString &path);
    void removeExcludePath(const QString &path);
    QStringList excludePaths() const { return m_excludePaths; }

    /**
     * Re-computes the whole model by iterating and parsing all dirs/files
     */
    void update();

    QVector<const Directory*> rootDirs() const;

    int fileCount() const { return m_fileCount; }
    int analyzedFileCount() const { return m_analyzedFileCount; }
    int dirCount() const { return m_dirCount; }

signals:
    void stateChanged();
    void fileCountChanged();
    void dirCountChanged();
    void analyzedFileCountChanged();
    void cacheDataChanged(const QByteArray &data);

public slots:
    void cancelUpdate();

private:
    void setState(State state);
    void setDirCount(int dirCount);
    void setFileCount(int fileCount);
    void setAnalyzedFileCount(int analzedFileCount);
    void clear();
    void recompute();
    void enumerate(Directory *dir);
    void analyze(Directory *dir);

    State m_state = State_Empty;

    QStringList m_fileEndings;
    QStringList m_rootDirNames;
    QStringList m_excludePaths;
    QStringList m_excludeAbsolutePaths;
    QHash<QString, Directory*> m_rootDirs;

    int m_fileCount = 0;
    int m_analyzedFileCount = 0;
    int m_dirCount = 0;

    std::atomic<int> m_abortFlag;

    CodeModelCache m_cache;
};
