#pragma once

#include <QDialog>
#include <QStringListModel>

QT_BEGIN_NAMESPACE
namespace Ui { class CodeModelDialog; }
QT_END_NAMESPACE

class CodeModelDialog : public QDialog
{
    Q_OBJECT

public:
    CodeModelDialog(QWidget *parent = nullptr);
    ~CodeModelDialog();

    void clear();

    void setFolders(const QStringList &f);
    void setExcluded(const QStringList &f);
    void setEndings(const QStringList &f);

    QStringList folders() const { return m_folderModel->stringList(); }
    QStringList excluded() const { return m_excludedModel->stringList(); }
    QStringList endings() const { return m_endingsModel->stringList(); }

signals:
    void accepted();
    void cancelled();

protected:
    virtual void resizeEvent(QResizeEvent *event) override;

private:
    Ui::CodeModelDialog *ui;
    QStringListModel *m_folderModel;
    QStringListModel *m_excludedModel;
    QStringListModel *m_endingsModel;
};
