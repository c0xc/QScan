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

#include "gui/pagelistwidget.hpp"

PageListWidget::PageListWidget(Document *doc, QWidget *parent)
              : QWidget(parent),
                m_document(doc)
{
    setupUi();
    
    // Connect document signals
    if (m_document)
    {
        connect(m_document, SIGNAL(pageAdded(int)),
                this, SLOT(onDocumentPageAdded(int)));
        connect(m_document, SIGNAL(pageRemoved(int)),
                this, SLOT(onDocumentPageRemoved(int)));
    }
}

void
PageListWidget::refresh()
{
    m_list->clear();
    
    if (!m_document)
        return;
    
    for (int i = 0; i < m_document->pageCount(); ++i)
    {
        updateThumbnail(i);
    }
}

int
PageListWidget::selectedPageIndex() const
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return -1;
    
    return m_list->row(item);
}

void
PageListWidget::onItemSelectionChanged()
{
    int index = selectedPageIndex();
    if (index >= 0)
    {
        emit pageSelected(index);
    }
}

void
PageListWidget::onAddPageClicked()
{
    emit addPageRequested();
}

void
PageListWidget::onDeletePageClicked()
{
    int index = selectedPageIndex();
    if (index >= 0)
    {
        emit deletePageRequested(index);
    }
}

void
PageListWidget::onDocumentPageAdded(int index)
{
    updateThumbnail(index);
}

void
PageListWidget::onDocumentPageRemoved(int index)
{
    QListWidgetItem *item = m_list->item(index);
    if (item)
    {
        delete item;
    }
}

void
PageListWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    // List widget
    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(120, 170));
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setMovement(QListWidget::Static);
    layout->addWidget(m_list);
    
    connect(m_list, SIGNAL(itemSelectionChanged()),
            this, SLOT(onItemSelectionChanged()));
    
    // Buttons
    QHBoxLayout *button_layout = new QHBoxLayout;
    
    m_btn_add = new QPushButton(tr("Add Page"));
    connect(m_btn_add, SIGNAL(clicked()), this, SLOT(onAddPageClicked()));
    button_layout->addWidget(m_btn_add);
    
    m_btn_delete = new QPushButton(tr("Delete Page"));
    connect(m_btn_delete, SIGNAL(clicked()), this, SLOT(onDeletePageClicked()));
    button_layout->addWidget(m_btn_delete);
    
    layout->addLayout(button_layout);
}

void
PageListWidget::updateThumbnail(int index)
{
    if (!m_document || index < 0 || index >= m_document->pageCount())
        return;
    
    const ScannedPage &page = m_document->page(index);
    QPixmap thumbnail = page.thumbnail(QSize(120, 170));
    
    // Create or update list item
    QListWidgetItem *item = m_list->item(index);
    if (!item)
    {
        item = new QListWidgetItem;
        m_list->insertItem(index, item);
    }
    
    item->setIcon(QIcon(thumbnail));
    item->setText(tr("Page %1").arg(index + 1));
}
