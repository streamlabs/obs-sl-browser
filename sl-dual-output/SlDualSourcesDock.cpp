#include "SlDualSourcesDock.hpp"
#include "SlDualDock.hpp"
#include "SlDualEditor.hpp"
#include "SlDualSourceList.hpp"

#include <QAbstractItemModel>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

SlDualSourcesDock::SlDualSourcesDock(SlDualController& controller, SlDualPreview* preview)
	: QWidget(nullptr),
	  m_controller(controller),
	  m_preview(preview)
{
	m_list = new SlDualSourceList(controller, preview, this);

	m_addButton = new QToolButton(this);
	m_addButton->setText("+");
	m_addButton->setToolTip("Add source");

	m_removeButton = new QToolButton(this);
	m_removeButton->setText("-");
	m_removeButton->setToolTip("Remove selected sources");

	m_propertiesButton = new QToolButton(this);
	m_propertiesButton->setText(QString(QChar(0x2699)));
	m_propertiesButton->setToolTip("Source properties");

	m_upButton = new QToolButton(this);
	m_upButton->setText(QString(QChar(0x25B2)));
	m_upButton->setToolTip("Move source up");

	m_downButton = new QToolButton(this);
	m_downButton->setText(QString(QChar(0x25BC)));
	m_downButton->setToolTip("Move source down");

	auto* toolbar = new QHBoxLayout();
	toolbar->setContentsMargins(0, 0, 0, 0);
	toolbar->addWidget(m_addButton);
	toolbar->addWidget(m_removeButton);
	toolbar->addWidget(m_propertiesButton);
	toolbar->addStretch(1);
	toolbar->addWidget(m_upButton);
	toolbar->addWidget(m_downButton);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(m_list, 1);
	layout->addLayout(toolbar);

	QObject::connect(m_addButton, &QToolButton::clicked, this, [this]() { onAdd(); });
	QObject::connect(m_removeButton, &QToolButton::clicked, this, [this]() { m_list->removeSelected(); });
	QObject::connect(m_propertiesButton, &QToolButton::clicked, this, [this]() { m_list->openSelectedProperties(); });
	QObject::connect(m_upButton, &QToolButton::clicked, this, [this]() { m_list->moveSelected(-1); });
	QObject::connect(m_downButton, &QToolButton::clicked, this, [this]() { m_list->moveSelected(1); });
	QObject::connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
	QObject::connect(m_list->model(), &QAbstractItemModel::rowsInserted, this, [this]() { updateButtons(); });
	QObject::connect(m_list->model(), &QAbstractItemModel::rowsRemoved, this, [this]() { updateButtons(); });

	refreshBinding();
}

void SlDualSourcesDock::refreshBinding()
{
	m_list->bindActiveScene();
	updateButtons();
}

void SlDualSourcesDock::onAdd()
{
	if (!m_preview)
		return;

	QPoint below(0, m_addButton->height());
	m_preview->editor().showAddSourceMenu(m_addButton->mapToGlobal(below), this);
}

void SlDualSourcesDock::updateButtons()
{
	// The list disables itself when no scene is bound.
	bool haveScene = m_list->isEnabled();
	bool haveSelection = !m_list->selectedItems().isEmpty();

	m_addButton->setEnabled(haveScene);
	m_removeButton->setEnabled(haveScene && haveSelection);
	m_propertiesButton->setEnabled(haveScene && haveSelection);
	m_upButton->setEnabled(haveScene && haveSelection && m_list->count() > 1);
	m_downButton->setEnabled(haveScene && haveSelection && m_list->count() > 1);
}
