#pragma once

#include <QMap>

#include "codemodel.h"

namespace FileEndingStats {

    struct Entry
    {
        QString ending;
        int fileCount = 0;
        int loc = 0;
    };

    using Stats = QVector<Entry>;
    void mergeStats(Stats &dst, const Stats &other);

    struct DirStats
    {
        QHash<const Directory*, Stats> perDir;
        Stats total;
    };

    DirStats getDirStats(const QVector<const Directory*> dirs, const QStringList &excludeList);

} // namespace FileEndingStats
