/****************************************************************************
**
** Copyright (C) 2026 Philip Seeger (p@c0xc.net)
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

#ifndef CORE_RUNTIME_SELFCHECK_HPP
#define CORE_RUNTIME_SELFCHECK_HPP

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace qscan
{

struct RuntimeSelfCheckResult
{
    bool scanner_enumeration_ok;
    bool camera_enumeration_ok;
    int scanner_device_count;
    int camera_device_count;
    QStringList notes;
    QStringList warnings;
    QStringList errors;

    RuntimeSelfCheckResult();

    bool
    isFailure() const;

    bool
    isDegraded() const;

    QString
    statusLabel() const;
};

RuntimeSelfCheckResult
runRuntimeSelfCheck(bool full_probe);

QString
runtimeSelfCheckToText(const RuntimeSelfCheckResult &result);

QByteArray
runtimeSelfCheckToJson(const RuntimeSelfCheckResult &result);

int
runtimeSelfCheckExitCode(const RuntimeSelfCheckResult &result);

} //namespace qscan

#endif //CORE_RUNTIME_SELFCHECK_HPP
