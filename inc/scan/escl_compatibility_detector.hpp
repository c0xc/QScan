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

#ifndef SCAN_ESCL_COMPATIBILITY_DETECTOR_HPP
#define SCAN_ESCL_COMPATIBILITY_DETECTOR_HPP

#include <QByteArray>
#include <QString>

namespace escl
{

enum class CompatibilityResult
{
    Compatible,
    SoapHt,
    NotEscl,
    Unknown
};

struct DetectionResult
{
    CompatibilityResult result = CompatibilityResult::Unknown;
    bool port8289Open = false;
    QString serverHeader;
};

bool
isSoapHtPortOpen(const QString &host, int timeout_ms = 2000);

bool
fetchScannerCapabilities(const QString &host, int timeout_ms, QString *server_header_out, QByteArray *xml_out);

CompatibilityResult
detectCompatibility(const QString &host, int timeout_ms, DetectionResult *detail_out);

bool
isCompatible(CompatibilityResult result);

} //namespace escl

#endif //SCAN_ESCL_COMPATIBILITY_DETECTOR_HPP
