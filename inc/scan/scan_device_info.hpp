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

#ifndef SCAN_SCAN_DEVICE_INFO_HPP
#define SCAN_SCAN_DEVICE_INFO_HPP

//#include <QObject> //only needed for tr() of type strings
#include <QString>

enum class ScanDeviceType
{
    UNKNOWN,
    SCANNER,
    CAMERA,
    MOBILE
};

struct ScanDeviceInfo
{
    QString name;
    QString description;
    ScanDeviceType type;
    bool selectable;

    ScanDeviceInfo()
        : type(ScanDeviceType::UNKNOWN),
          selectable(true)
    {
    }

    ScanDeviceInfo(const QString &n, const QString &d, ScanDeviceType t, bool is_selectable = true)
        : name(n), description(d), type(t), selectable(is_selectable)
    {
    }

    QString typeString() const
    {
        switch (type)
        {
            case ScanDeviceType::SCANNER: return QStringLiteral("Scanner");
            case ScanDeviceType::CAMERA:  return QStringLiteral("Camera");
            case ScanDeviceType::MOBILE:  return QStringLiteral("Mobile");
            default:                      return QStringLiteral("Unknown");
        }
    }
};

#endif //SCAN_SCAN_DEVICE_INFO_HPP
