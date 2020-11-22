#include "progressbar.h"
#include "util.h"

#include <QVBoxLayout>
#include <QPushButton>

ProgressBar::ProgressBar(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Generating TreeMap Data...");

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    m_label = new QLabel(this);
    m_label->setText("");
    layout->addWidget(m_label);

    QPushButton *cancelButton = new QPushButton(this);
    cancelButton->setText("Cancel");
    connect(cancelButton, &QPushButton::clicked, this, &ProgressBar::abort);
    layout->addWidget(cancelButton);
}

void ProgressBar::enumerating(int dirs, int files)
{
    setVisible(true);
    setModal(true);

    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_label->setText(QString("Enumerating... (%1 dirs, %2 files)")
                    .arg(formatNumDecimals(dirs))
                    .arg(formatNumDecimals(files)));
}

void ProgressBar::analyzing(int done, int total)
{
    setVisible(true);
    setModal(true);

    m_progressBar->setRange(0, total);
    m_progressBar->setValue(done);
    m_label->setText(QString("Analyzing... (%1/%2 files)")
                     .arg(formatNumDecimals(done))
                     .arg(formatNumDecimals(total)));
}

void ProgressBar::ready()
{
    setVisible(false);
}
