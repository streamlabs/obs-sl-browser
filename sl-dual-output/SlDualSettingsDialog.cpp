#include "SlDualSettingsDialog.hpp"

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

namespace {

struct SizePreset {
	const char *label;
	uint32_t width;
	uint32_t height;
};

const SizePreset kPresets[] = {
	{"1080 x 1920 (9:16 vertical)", 1080, 1920},
	{"720 x 1280 (9:16 vertical)", 720, 1280},
	{"1080 x 1080 (1:1 square)", 1080, 1080},
};
const int kCustomIndex = 3;

} // namespace

SlDualSettingsDialog::SlDualSettingsDialog(const SlDualConfig &current, bool streamActive, QWidget *parent)
	: QDialog(parent),
	  m_base(current)
{
	setWindowTitle("Dual Output Settings");
	setMinimumWidth(420);

	auto *form = new QFormLayout();

	// Canvas size
	m_sizePreset = new QComboBox(this);
	for (const SizePreset &preset : kPresets)
		m_sizePreset->addItem(preset.label);
	m_sizePreset->addItem("Custom");

	m_width = new QSpinBox(this);
	m_width->setRange(32, 8192);
	m_width->setSingleStep(2);
	m_width->setValue((int)current.canvasWidth);

	m_height = new QSpinBox(this);
	m_height->setRange(32, 8192);
	m_height->setSingleStep(2);
	m_height->setValue((int)current.canvasHeight);

	auto *sizeRow = new QHBoxLayout();
	sizeRow->addWidget(m_width);
	sizeRow->addWidget(new QLabel("x", this));
	sizeRow->addWidget(m_height);
	sizeRow->addStretch(1);

	form->addRow("Canvas size:", m_sizePreset);
	form->addRow(QString(), sizeRow);

	QObject::connect(m_sizePreset, &QComboBox::currentIndexChanged, this,
			 [this](int index) { onPresetChanged(index); });

	int presetIndex = presetIndexFor(current.canvasWidth, current.canvasHeight);
	m_sizePreset->setCurrentIndex(presetIndex);
	onPresetChanged(presetIndex);

	// Destination
	m_server = new QLineEdit(QString::fromUtf8(current.server.c_str()), this);
	m_server->setPlaceholderText("rtmp://...");
	form->addRow("Server:", m_server);

	m_key = new QLineEdit(QString::fromUtf8(current.key.c_str()), this);
	m_key->setEchoMode(QLineEdit::Password);
	m_showKey = new QCheckBox("Show", this);
	QObject::connect(m_showKey, &QCheckBox::toggled, this, [this](bool checked) {
		m_key->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
	});

	auto *keyRow = new QHBoxLayout();
	keyRow->addWidget(m_key, 1);
	keyRow->addWidget(m_showKey);
	form->addRow("Stream key:", keyRow);

	// Encoding
	m_encoder = new QComboBox(this);
	populateEncoders(current.encoderId);
	form->addRow("Video encoder:", m_encoder);

	m_videoBitrate = new QSpinBox(this);
	m_videoBitrate->setRange(500, 100000);
	m_videoBitrate->setSingleStep(250);
	m_videoBitrate->setSuffix(" kbps");
	m_videoBitrate->setValue(current.videoBitrateKbps);
	form->addRow("Video bitrate:", m_videoBitrate);

	m_audioTrack = new QComboBox(this);
	for (int i = 1; i <= (int)MAX_AUDIO_MIXES; i++)
		m_audioTrack->addItem(QString("Track %1").arg(i));
	m_audioTrack->setCurrentIndex(std::clamp(current.audioTrack, 1, (int)MAX_AUDIO_MIXES) - 1);
	form->addRow("Audio track:", m_audioTrack);

	m_audioBitrate = new QSpinBox(this);
	m_audioBitrate->setRange(64, 320);
	m_audioBitrate->setSingleStep(32);
	m_audioBitrate->setSuffix(" kbps");
	m_audioBitrate->setValue(current.audioBitrateKbps);
	form->addRow("Audio bitrate:", m_audioBitrate);

	m_autoStart = new QCheckBox("Start and stop with the main stream", this);
	m_autoStart->setChecked(current.autoStart);
	form->addRow(QString(), m_autoStart);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);

	if (streamActive) {
		m_sizePreset->setEnabled(false);
		m_width->setEnabled(false);
		m_height->setEnabled(false);
		auto *note = new QLabel("Canvas size is locked while the dual output stream is live.", this);
		note->setWordWrap(true);
		layout->addWidget(note);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

int SlDualSettingsDialog::presetIndexFor(uint32_t width, uint32_t height) const
{
	for (int i = 0; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); i++) {
		if (kPresets[i].width == width && kPresets[i].height == height)
			return i;
	}
	return kCustomIndex;
}

void SlDualSettingsDialog::onPresetChanged(int index)
{
	bool custom = index == kCustomIndex;
	m_width->setEnabled(custom && m_sizePreset->isEnabled());
	m_height->setEnabled(custom && m_sizePreset->isEnabled());

	if (!custom && index >= 0 && index < kCustomIndex) {
		m_width->setValue((int)kPresets[index].width);
		m_height->setValue((int)kPresets[index].height);
	}
}

void SlDualSettingsDialog::populateEncoders(const std::string &currentId)
{
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;

		const char *codec = obs_get_encoder_codec(id);
		if (!codec || strcmp(codec, "h264") != 0)
			continue;

		uint32_t caps = obs_get_encoder_caps(id);
		if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;

		const char *display = obs_encoder_get_display_name(id);
		m_encoder->addItem(QString::fromUtf8(display ? display : id), QString::fromUtf8(id));
	}

	int index = m_encoder->findData(QString::fromUtf8(currentId.c_str()));
	if (index < 0 && !currentId.empty()) {
		m_encoder->addItem(QString::fromUtf8(currentId.c_str()), QString::fromUtf8(currentId.c_str()));
		index = m_encoder->count() - 1;
	}
	m_encoder->setCurrentIndex(index >= 0 ? index : 0);
}

SlDualConfig SlDualSettingsDialog::resultConfig() const
{
	SlDualConfig config = m_base;

	config.canvasWidth = (uint32_t)m_width->value();
	config.canvasHeight = (uint32_t)m_height->value();
	config.server = m_server->text().trimmed().toUtf8().constData();
	config.key = m_key->text().trimmed().toUtf8().constData();
	config.encoderId = m_encoder->currentData().toString().toUtf8().constData();
	config.videoBitrateKbps = m_videoBitrate->value();
	config.audioBitrateKbps = m_audioBitrate->value();
	config.audioTrack = m_audioTrack->currentIndex() + 1;
	config.autoStart = m_autoStart->isChecked();

	return config;
}
