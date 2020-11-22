#include "util.h"

QString formatNumDecimals(int num)
{
    if (num < 1000)
        return QString::number(num);

    QString remainder = QString::asprintf("%03d", num % 1000);
    return formatNumDecimals(num / 1000) + "." + remainder;
}
