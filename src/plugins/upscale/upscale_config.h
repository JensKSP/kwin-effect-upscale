/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KCModule>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace KWin
{

class UpscaleEffectConfig : public KCModule
{
    Q_OBJECT

public:
    explicit UpscaleEffectConfig(QObject *parent, const KPluginMetaData &data);
    void load() override;
    void save() override;
    void defaults() override;

private:
    void updatePreview();
    void refreshStatus();
    void updateOutputs();

    QCheckBox *m_enabled;
    QComboBox *m_output;
    QComboBox *m_preset;
    QSlider *m_percentage;
    QLabel *m_preview;
    QCheckBox *m_sharpening;
    QSlider *m_strength;
    QLabel *m_strengthLabel;
    QLabel *m_status;
};

} // namespace KWin
