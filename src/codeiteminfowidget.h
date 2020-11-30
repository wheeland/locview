#ifndef CODEITEMINFOWIDGET_H
#define CODEITEMINFOWIDGET_H

#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include "codemodel.h"

class CodeItemInfoWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit CodeItemInfoWidget(QWidget *parent = nullptr);
    ~CodeItemInfoWidget();

    void setCodeItem(CodeItem *item);
    CodeItem *codeItem() const { return m_codeItem; }

    void setExcludeList(const QStringList &excludes) { m_excludes = excludes; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupWidgets();
    void update();

    QLabel *fullPath;
    QLabel *loc;
    QLabel *label;

    QStringList m_excludes;

    CodeItem *m_codeItem = nullptr;
};

#endif // CODEITEMINFOWIDGET_H
