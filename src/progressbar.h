#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QLabel>

class ProgressBar : public QDialog
{
    Q_OBJECT

public:
    ProgressBar(QWidget *parent);
    ~ProgressBar() = default;

public slots:
    void enumerating(int dirs, int files);
    void analyzing(int done, int total);
    void ready();

signals:
    void abort();

private:
    QProgressBar *m_progressBar;
    QLabel *m_label;
};
