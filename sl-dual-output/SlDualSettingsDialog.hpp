#pragma once

// Module-internal. Canvas size, destination, encoder and behavior settings.

#include "SlDualConfig.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

class SlDualSettingsDialog : public QDialog
{
public:
	SlDualSettingsDialog(const SlDualConfig &current, bool streamBusy, QWidget *parent);

	SlDualConfig resultConfig() const;

private:
	void populateEncoders(const std::string &currentId);
	void onPresetChanged(int index);
	int presetIndexFor(uint32_t width, uint32_t height) const;

	SlDualConfig m_base;

	QComboBox *m_sizePreset = nullptr;

	// Index of the "Custom (W x H)" entry, or -1 when the size matched a preset and none was added.
	int m_customIndex = -1;
	QSpinBox *m_width = nullptr;
	QSpinBox *m_height = nullptr;
	QLineEdit *m_server = nullptr;
	QLineEdit *m_key = nullptr;
	QCheckBox *m_showKey = nullptr;
	QCheckBox *m_useAuth = nullptr;
	QLineEdit *m_authUsername = nullptr;
	QLineEdit *m_authPassword = nullptr;
	QComboBox *m_encoder = nullptr;
	QSpinBox *m_videoBitrate = nullptr;
	QSpinBox *m_audioBitrate = nullptr;
	QComboBox *m_audioTrack = nullptr;
	QCheckBox *m_autoStart = nullptr;
};
