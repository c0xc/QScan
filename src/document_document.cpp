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

#include "document/document.hpp"

Document::Document(QObject *parent)
        : QObject(parent),
          m_mode(IMAGE_MODE),
          m_creator_program(QStringLiteral("QScan")),
          m_creator_author(QStringLiteral("c0xc")),
          m_created_time(QDateTime::currentDateTime())
{
}

void
Document::setCreatorProgram(const QString &name)
{
    m_creator_program = name;
}

QString
Document::creatorProgram() const
{
    return m_creator_program;
}

void
Document::setCreatorAuthor(const QString &author)
{
    m_creator_author = author;
}

QString
Document::creatorAuthor() const
{
    return m_creator_author;
}

QDateTime
Document::createdTime() const
{
    return m_created_time;
}

void
Document::addPage(const ScannedPage &page)
{
    m_pages.append(page);
    emit pageAdded(m_pages.size() - 1);
}

void
Document::insertPage(int index, const ScannedPage &page)
{
    m_pages.insert(index, page);
    emit pageAdded(index);
}

void
Document::removePage(int index)
{
    if (index >= 0 && index < m_pages.size())
    {
        m_pages.removeAt(index);
        emit pageRemoved(index);
    }
}

void
Document::movePage(int from, int to)
{
    if (from >= 0 && from < m_pages.size() && to >= 0 && to < m_pages.size())
    {
        m_pages.move(from, to);
        emit pagesReordered();
    }
}

ScannedPage&
Document::page(int index)
{
    return m_pages[index];
}

const ScannedPage&
Document::page(int index) const
{
    return m_pages[index];
}

int
Document::pageCount() const
{
    return m_pages.size();
}

void
Document::clear()
{
    m_pages.clear();
}

void
Document::setScanMode(ScanMode mode)
{
    m_mode = mode;
}

Document::ScanMode
Document::scanMode() const
{
    return m_mode;
}
