#pragma once

#include <QString>
#include <QByteArray>

class PersistentData
{
public:
    static QByteArray getCacheData();
    static void setCacheData(const QByteArray &data);

    static QStringList getIncludePaths();
    static void setIncludePaths(const QStringList &strings);

    static QStringList getExcludePaths();
    static void setExcludePaths(const QStringList &strings);

    static QStringList getFileEndings();
    static void setFileEndings(const QStringList &strings);
};
