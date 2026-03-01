/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (p@c0xc.net)
** This file is part of QScan.
**
** QScan is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** QScan is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with QScan. If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

#include <QUrl>

#include "scan/escl_url.hpp"

QString
esclNormalizeBaseUrlForStorage(const QString &input,
                              const QString &default_scheme,
                              QString *error_out)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
        if (error_out)
            *error_out = QStringLiteral("URL is empty");
        return QString();
    }

    QString url_text = trimmed;
    if (!url_text.contains("://"))
        url_text = QStringLiteral("%1://%2").arg(default_scheme, url_text);

    QUrl url(url_text);
    if (!url.isValid() || url.host().isEmpty())
    {
        if (error_out)
            *error_out = QStringLiteral("Invalid URL");
        return QString();
    }

    QString path = url.path();
    if (path.isEmpty())
        path = QStringLiteral("/");

    const int idx = path.indexOf(QStringLiteral("/eSCL"));
    if (idx >= 0)
        path = path.left(idx + 5);
    else if (path == QStringLiteral("/"))
        path = QStringLiteral("/eSCL");

    url.setPath(path);
    return url.toString(QUrl::NormalizePathSegments);
}
