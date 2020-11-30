#include "codeiteminfowidget.h"
#include "util.h"
#include "codeutil.h"

#include <QSizePolicy>

CodeItemInfoWidget::CodeItemInfoWidget(QWidget *parent) :
    QGroupBox(parent)
{
    setupWidgets();

    QFont font = label->font();
    font.setWeight(QFont::Bold);
    label->setFont(font);
}

CodeItemInfoWidget::~CodeItemInfoWidget()
{
}

void CodeItemInfoWidget::setupWidgets()
{
    setTitle("");

    QSizePolicy sizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(this->sizePolicy().hasHeightForWidth());
    setSizePolicy(sizePolicy);

    setMinimumHeight(150);
    setMaximumHeight(150);

    QVBoxLayout *verticalLayout = new QVBoxLayout(this);
    verticalLayout->setContentsMargins(0, 0, 0, 0);

    label = new QLabel(this);
    verticalLayout->addWidget(label);

    loc = new QLabel(this);
    verticalLayout->addWidget(loc);

    fullPath = new QLabel(this);
    fullPath->setWordWrap(true);
    verticalLayout->addWidget(fullPath);

    verticalLayout->addStretch();
}

void CodeItemInfoWidget::setCodeItem(CodeItem *item)
{
    if (m_codeItem != item) {
        m_codeItem = item;
        update();
    }
}

void CodeItemInfoWidget::resizeEvent(QResizeEvent *event)
{
    label->setMinimumWidth(width() - 10);
    label->setMaximumWidth(width() - 10);
    loc->setMinimumWidth(width() - 10);
    loc->setMaximumWidth(width() - 10);
    fullPath->setMinimumWidth(width() - 10);
    fullPath->setMaximumWidth(width() - 10);
}

void CodeItemInfoWidget::update()
{
    if (!m_codeItem) {
        label->setText("");
        fullPath->setText("");
        loc->setText("");
    }
    else if (m_codeItem->type() == CodeItem::Type_Directory) {
        int dirs = 0, files = 0;
        m_codeItem->traverse([&](const File*) { files++; });
        m_codeItem->traverse([&](const Directory*) { dirs++; }, CodeItem::ItemFirst);

        Directory *dir = (Directory*) m_codeItem;
        label->setText(dir->name() + " (Directory)");
        fullPath->setText(dir->fullName());

        QString text = QString("%1 loc (%2 dirs, %3 files)")
                     .arg(formatNumDecimals(dir->loc()))
                     .arg(formatNumDecimals(dirs))
                     .arg(formatNumDecimals(files));

        const FileEndingStats::DirStats dirStats = FileEndingStats::getDirStats({dir}, m_excludes);
        for (const FileEndingStats::Entry &entry : dirStats.total) {
            text += QString::asprintf("\n*.%1 (%2 loc, %3 files)")
                    .arg(entry.ending)
                    .arg(formatNumDecimals(entry.loc))
                    .arg(formatNumDecimals(entry.fileCount));
        }

        loc->setText(text);
    }
    else if (m_codeItem->type() == CodeItem::Type_File) {
        File *file = (File*) m_codeItem;
        label->setText(file->name() + "." + file->ending());
        fullPath->setText(file->fullName());
        loc->setText(QString("%1 loc").arg(formatNumDecimals(file->loc())));
    }
}
