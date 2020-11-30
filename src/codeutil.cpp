#include "codeutil.h"

namespace FileEndingStats {

static Entry &getEntry(Stats &stats, const QString &ending)
{
    for (Entry &entry : stats) {
        if (entry.ending == ending)
            return entry;
    }
    stats << Entry{ending, 0, 0};
    return stats.last();
}

void mergeStats(Stats &dst, const Stats &other)
{
    for (auto it = other.begin(); it != other.end(); ++it) {
        Entry &entry = getEntry(dst, it->ending);
        entry.loc += it->loc;
        entry.fileCount += it->fileCount;
    }
}

DirStats getDirStats(const QVector<const Directory*> dirs, const QStringList &excludeList)
{
    DirStats ret;

    for (const Directory *rootDir : dirs) {
        if (excludeList.contains(rootDir->path()))
            continue;

        rootDir->traverse([&](const Directory *dir) {
            Stats &endings = ret.perDir[dir];

            for (const CodeItem *child : dir->children()) {
                if (excludeList.contains(child->path()))
                    continue;

                if (child->type() == CodeItem::Type_File) {
                    File *file = (File*) child;
                    Entry &entry = getEntry(endings, file->ending());
                    entry.loc += file->loc();
                    entry.fileCount += 1;
                }

                if (child->type() == CodeItem::Type_Directory) {
                    Q_ASSERT(ret.perDir.contains((Directory*) child));
                    mergeStats(endings, ret.perDir[(Directory*) child]);
                }
            }

            std::sort(endings.begin(), endings.end(), [](const Entry &a, const Entry &b) {
                return a.loc > b.loc;
            });
        }, CodeItem::ChildrenFirst);

        mergeStats(ret.total, ret.perDir[rootDir]);
    }

    return ret;
}

} // namespace Stats
