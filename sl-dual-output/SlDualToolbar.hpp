#pragma once

// Module-internal.
// Dock toolbar helpers: OBS-native toolbar actions using OBS's own compiled-in icons and theme classes.
// Themes restyle the buttons through the same selectors as the main Scenes/Sources docks (e.g. Yami's .icon-plus / OBSDock QToolBar rules).

#include <QAction>
#include <QIcon>
#include <QToolBar>

inline QAction* slDualToolAction(QToolBar* bar, const char* iconResource, const char* themeClass, const char* text, const char* toolTip)
{
	QAction* action = bar->addAction(QIcon(QString::fromUtf8(iconResource)), QString::fromUtf8(text));
	action->setToolTip(QString::fromUtf8(toolTip));
	action->setProperty("class", themeClass);
	return action;
}

// Theme QSS matches widgets, not actions; copy the actions' dynamic properties onto the generated buttons, same as OBSBasic::copyActionsDynamicProperties().
inline void slDualApplyThemeProperties(QToolBar* bar)
{
	for (QAction* action : bar->actions())
	{
		QWidget* button = bar->widgetForAction(action);

		if (!button)
			continue;

		for (QByteArray& name : action->dynamicPropertyNames())
			button->setProperty(name.constData(), action->property(name.constData()));
	}
}
