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

#ifndef CORE_MAIN_HPP
#define CORE_MAIN_HPP

#include <QCoreApplication>
#include <QApplication>
#include <QTranslator>
#include <QProcessEnvironment>
#include <QFontDatabase>
#include <QMessageBox>

#include "document/document.hpp"
#include "gui/mainwindow.hpp"
#include "scan/scanmanager.hpp"
#include "gui/scannerselector.hpp"

#include "core/classlogger.hpp"

#define PROGRAM "QScan"

#endif // CORE_MAIN_HPP
