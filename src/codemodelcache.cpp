#include "codemodelcache.h"

#include <QDataStream>
#include <QFile>
#include <QCryptographicHash>

CodeModelCache::CodeModelCache()
{
}

CodeModelCache::~CodeModelCache()
{
}

bool CodeModelCache::getEntry(const QString &path, qint64 sz, const QDateTime &dt, int &loc) const
{
    const QByteArray key = hash(path, sz, dt);
    const auto it = m_entries.find(key);

    if (it == m_entries.end())
        return false;

    loc = it.value();
    return true;
}

void CodeModelCache::saveEntry(const QString &path, qint64 sz, const QDateTime &dt, int loc)
{
    const QByteArray key = hash(path, sz, dt);
    m_entries[key] = loc;
}

QByteArray CodeModelCache::serialize() const
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);

    out << m_entries.size();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        out << it.key();
        out << it.value();
    }

    return data;
}

bool CodeModelCache::deserialize(const QByteArray &data)
{
    QHash<QByteArray, int> entries;

    QDataStream in(data);
    int sz;
    in >> sz;
    for (int i = 0; i < sz; ++i) {
        QByteArray hash;
        int loc;
        in >> hash;
        in >> loc;
        entries[hash] = loc;
    }

    if (in.status() != QDataStream::Ok)
        return false;

    m_entries = entries;
    return true;
}

static QByteArray pack(const QString &path, qint64 sz, const QDateTime &dt)
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << path.toUtf8();
    out << sz;
    out << dt;
    return data;
}

QByteArray CodeModelCache::hash(const QString &path, qint64 sz, const QDateTime &dt)
{
    const QByteArray data = pack(path, sz, dt);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(data);
    return hash.result();
}
