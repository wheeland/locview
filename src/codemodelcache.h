#pragma once

#include <QString>
#include <QHash>
#include <QDateTime>

class CodeModelCache
{
public:
    CodeModelCache();
    ~CodeModelCache();

    bool getEntry(const QString &path, qint64 sz, const QDateTime &dt, int &loc) const;
    void saveEntry(const QString &path, qint64 sz, const QDateTime &dt, int loc);

    QByteArray serialize() const;
    bool deserialize(const QByteArray &data);

private:
    static QByteArray hash(const QString &path, qint64 sz, const QDateTime &dt);

    // files are indexed by a hash of (fileName, size, lastModified)
    QHash<QByteArray, int> m_entries;
};
