/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (philip@c0xc.net)
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

#ifndef SCAN_ESCL_PROBE_HPP
#define SCAN_ESCL_PROBE_HPP

#include <QByteArray>
#include <QString>
#include <QUrl>

bool
esclProbeScannerCapabilities(const QUrl &escl_base_url,
                            int timeout_ms,
                            QString *error_out,
                            QByteArray *caps_xml_out,
                            QUrl *effective_base_url_out = 0);

#endif //SCAN_ESCL_PROBE_HPP
