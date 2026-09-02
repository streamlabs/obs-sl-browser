#include "SlDualTransitionsDock.hpp"
#include "SlDualTransitions.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

// Same button construction as the main transitions dock: OBS's icons plus the btn-tool theme class.
static QPushButton* toolButton(QWidget* parent, const char* iconResource, const char* themeClass, const char* toolTip)
{
	auto* button = new QPushButton(parent);
	button->setIcon(QIcon(QString::fromUtf8(iconResource)));
	button->setToolTip(QString::fromUtf8(toolTip));
	button->setProperty("class", themeClass);
	button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	return button;
}

SlDualTransitionsDock::SlDualTransitionsDock(SlDualController& controller) : QWidget(nullptr), m_controller(controller)
{
	m_combo = new QComboBox(this);
	m_combo->setToolTip("Scene transition for the vertical canvas");

	m_durationLabel = new QLabel("Duration", this);

	m_duration = new QSpinBox(this);
	m_duration->setSuffix(" ms");
	m_duration->setMinimum(50);
	m_duration->setMaximum(20000);
	m_duration->setSingleStep(50);

	auto* durationRow = new QHBoxLayout();
	durationRow->setContentsMargins(0, 0, 0, 0);
	durationRow->addWidget(m_durationLabel);
	durationRow->addWidget(m_duration, 1);

	m_addButton = toolButton(this, ":/res/images/plus.svg", "btn-tool icon-plus", "Add configurable transition");
	m_removeButton = toolButton(this, ":/res/images/minus.svg", "btn-tool icon-trash", "Remove transition");
	m_propertiesButton = toolButton(this, ":/settings/images/settings/general.svg", "btn-tool icon-gear", "Transition properties");

	auto* buttonRow = new QHBoxLayout();
	buttonRow->setContentsMargins(0, 0, 0, 0);
	buttonRow->addStretch(1);
	buttonRow->addWidget(m_addButton);
	buttonRow->addWidget(m_removeButton);
	buttonRow->addWidget(m_propertiesButton);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(m_combo);
	layout->addLayout(durationRow);
	layout->addLayout(buttonRow);
	layout->addStretch(1);

	QObject::connect(m_combo, &QComboBox::currentIndexChanged, this, [this](int) { onSelectionChanged(); });
	QObject::connect(m_duration, &QSpinBox::valueChanged, this, [this](int value)
	{
		if (!m_updating)
			m_controller.transitionSetDuration(value);
	});
	QObject::connect(m_addButton, &QPushButton::clicked, this, [this]() { onAdd(); });
	QObject::connect(m_removeButton, &QPushButton::clicked, this, [this]() { onRemove(); });
	QObject::connect(m_propertiesButton, &QPushButton::clicked, this, [this]() { onProperties(); });

	refresh();
}

void SlDualTransitionsDock::refresh()
{
	SlDualTransitions* transitions = m_controller.transitions.get();

	m_updating = true;
	m_combo->clear();

	std::string selected;

	if (transitions)
	{
		selected = transitions->selectedName(m_controller.config);

		for (const std::string& name : transitions->names())
		{
			m_combo->addItem(QString::fromUtf8(name.c_str()));

			if (name == selected)
				m_combo->setCurrentIndex(m_combo->count() - 1);
		}
	}

	m_duration->setValue(m_controller.config.transitionDurationMs);
	m_updating = false;

	bool haveSelection = transitions && !selected.empty();
	obs_source_t* source = haveSelection ? transitions->find(selected) : nullptr;
	bool fixed = source && obs_transition_fixed(source);
	bool configurable = haveSelection && transitions->configurable(selected);

	// Like the main dock: fixed transitions (Cut) hide the duration, only configurable ones can be removed or configured.
	m_durationLabel->setVisible(!fixed);
	m_duration->setVisible(!fixed);
	m_combo->setEnabled(m_combo->count() > 0);
	m_addButton->setEnabled(transitions != nullptr);
	m_removeButton->setEnabled(configurable);
	m_propertiesButton->setEnabled(configurable);
}

void SlDualTransitionsDock::onSelectionChanged()
{
	if (m_updating || m_combo->currentIndex() < 0)
		return;

	m_controller.transitionSelect(m_combo->currentText().toUtf8().constData());
}

void SlDualTransitionsDock::onAdd()
{
	QMenu menu(this);
	size_t idx = 0;
	const char* id = nullptr;

	while (obs_enum_transition_types(idx++, &id))
	{
		if (!obs_is_source_configurable(id))
			continue;

		const char* display = obs_source_get_display_name(id);
		std::string idCopy = id;
		QString label = QString::fromUtf8(display ? display : id);
		menu.addAction(label, [this, idCopy, label]()
		{
			bool ok = false;
			QString name = QInputDialog::getText(this, "Add Transition", "Transition name:", QLineEdit::Normal, label, &ok);
			name = name.trimmed();

			if (!ok || name.isEmpty())
				return;

			if (!m_controller.transitionAdd(idCopy, name.toUtf8().constData()))
				QMessageBox::information(this, "Add Transition", "A transition with that name already exists.");
		});
	}

	menu.exec(m_addButton->mapToGlobal(QPoint(0, m_addButton->height())));
}

void SlDualTransitionsDock::onRemove()
{
	QString name = m_combo->currentText();

	if (name.isEmpty())
		return;

	if (QMessageBox::question(this, "Remove Transition", QString("Remove transition '%1'?").arg(name)) != QMessageBox::Yes)
		return;

	m_controller.transitionRemoveSelected();
}

void SlDualTransitionsDock::onProperties()
{
	SlDualTransitions* transitions = m_controller.transitions.get();

	if (!transitions)
		return;

	obs_source_t* source = transitions->selected(m_controller.config);

	if (source && obs_source_configurable(source))
		obs_frontend_open_source_properties(source);
}
