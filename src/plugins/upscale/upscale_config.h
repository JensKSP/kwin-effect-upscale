/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KCModule>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace KWin
{

class UpscaleApplicationEditor;

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
    void showSettings();
    void applySettings();
    void addDisplayControls(QFormLayout *layout);
    void addApplicationControls(QFormLayout *layout);
    void resetApplications();
    static void reconfigureEffect();
    void updateApplicationSummary();
    void addStatusControls(QFormLayout *layout);
    void connectControls();
    void showSupportInformation(const QString &information);
    static QString installedBuild();

    QCheckBox *m_enabled;
    QComboBox *m_output;
    QComboBox *m_preset;
    QSlider *m_percentage;
    QLabel *m_preview;
    QCheckBox *m_sharpening;
    QSlider *m_strength;
    QLabel *m_strengthLabel;
    QCheckBox *m_osd;
    QCheckBox *m_osdDetection;
    QCheckBox *m_osdSummary;
    QCheckBox *m_osdStatistics;
    QCheckBox *m_osdDeveloper;
    QSpinBox *m_osdTimeout;
    QCheckBox *m_unknown;
    UpscaleApplicationEditor *m_editor;
    QLabel *m_applications;
    QPushButton *m_resetApplications;
    QLabel *m_build;
    QLabel *m_status;
};

} // namespace KWin
