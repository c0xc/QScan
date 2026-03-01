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

#ifndef SCAN_WEBCAM_BACKEND_HPP
#define SCAN_WEBCAM_BACKEND_HPP

#include <QImage>
#include <QString>

//Backend interface for webcam capture implementations
class WebcamBackend
{
public:

    virtual
    ~WebcamBackend() = default;

    virtual bool
    initialize(const QString &device_id) = 0;

    virtual QImage
    captureFrame() = 0;

    virtual bool
    startPreview() = 0;

    virtual void
    stopPreview() = 0;

};

#endif //SCAN_WEBCAM_BACKEND_HPP
