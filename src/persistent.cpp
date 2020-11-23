#include "persistent.h"

#include <QSettings>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QThread>

static const QString KEY_CACHE_PATH("CacheFileLocation");
static const QString KEY_INCLUDES("IncludePaths");
static const QString KEY_EXCLUDES("ExcludePaths");
static const QString KEY_ENDINGS("FileEndings");
static const QString KEY_THREADCOUNT("CodeModelThreadCount");

static QString dataDirectory()
{
    QString dir;

    dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!dir.isEmpty())
        return dir;

    dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dir.isEmpty())
        return dir;

    dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!dir.isEmpty())
        return dir;

    dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!dir.isEmpty())
        return dir;

    dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dir.isEmpty())
        return dir;

    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

static QString cacheFilePath()
{
    static const QString dir = dataDirectory();
    QDir().mkpath(dir);

    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);

    QVariant path = settings.value(KEY_CACHE_PATH);
    if (!path.canConvert<QString>() || !QFile::exists(settings.value(KEY_CACHE_PATH).toString())) {
        path = dir + QDir::separator() + "cache.bin";
        settings.setValue(KEY_CACHE_PATH, path);
    }
    return path.toString();
}

QByteArray PersistentData::getCacheData()
{
    const QString path = cacheFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Can't read cache file" << path;
        return QByteArray();
    }

    return file.readAll();
}

void PersistentData::setCacheData(const QByteArray &data)
{
    const QString path = cacheFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Can't write cache file" << path;
        return;
    }

    file.write(data);
}

QStringList PersistentData::getIncludePaths()
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    return settings.value(KEY_INCLUDES).toStringList();
}

void PersistentData::setIncludePaths(const QStringList &strings)
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    settings.setValue(KEY_INCLUDES, strings);
    settings.sync();
}

QStringList PersistentData::getExcludePaths()
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    return settings.value(KEY_EXCLUDES).toStringList();
}

void PersistentData::setExcludePaths(const QStringList &strings)
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    settings.setValue(KEY_EXCLUDES, strings);
    settings.sync();
}

QStringList PersistentData::getFileEndings()
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    return settings.value(KEY_ENDINGS).toStringList();
}

void PersistentData::setFileEndings(const QStringList &strings)
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    settings.setValue(KEY_ENDINGS, strings);
    settings.sync();
}

int PersistentData::getCodeModelThreadCount()
{
    static QSettings settings(dataDirectory() + QDir::separator() + "settings.ini", QSettings::IniFormat);
    return settings.value(KEY_THREADCOUNT, QVariant(2 * QThread::idealThreadCount())).toInt();
}