/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "methodcontrols.h"
#include "settings.h"

#include <KCModule>

#include <QList>

#include <array>
#include <cstddef>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;

namespace KWin
{

class UpscaleApplicationEditor;
class UpscaleResolutionPreview;
class UpscaleSliderField;

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
    /** The global values as the page shows them, applied or not. */
    UpscaleSettings shownSettings() const;
    /** Shows the stored settings, through the previous release's keys where @p legacy. */
    void showSettings(bool legacy);
    void applySettings();
    void addDisplayControls(QFormLayout *layout);
    /** The three position boxes, in the order the stored corners are kept. */
    std::array<QComboBox *, 3> positionControls();
    /** Applies a corner the user just chose, moving whoever held it. */
    void takeCorner(std::size_t display);
    void addThresholdControl(QFormLayout *layout);
    void addApplicationControls(QFormLayout *layout);
    void addUnlistedControls(QFormLayout *layout);
    /** "All games": the global settings, in the tabs a game's entry has. */
    QTabWidget *buildAllPanel();
    static void alignLabels(const QList<QFormLayout *> &forms);
    void resetApplications();
    void exportApplications();
    void importApplications();
    /** Copy what the effect observed of a window the person picks; see upscaleSubmissionReport(). */
    void copyReport();
    void askForReport(const QString &window);
    static void reconfigureEffect();
    void updateApplicationSummary();
    void addAboutControls(QFormLayout *layout);
    void addProxyControls(QFormLayout *layout);
    void updateProxyStatus();
    void connectControls();
    static QString installedVersion();

    QComboBox *m_preset;
    QSlider *m_percentage;
    QComboBox *m_minimumPixels;
    UpscaleResolutionPreview *m_preview;
    QComboBox *m_geometry;
    QComboBox *m_filter;
    QCheckBox *m_sharpening;
    QCheckBox *m_x11Proxy = nullptr;
    QLabel *m_proxyStatus = nullptr;
    QSlider *m_strength;
    UpscaleSliderField *m_scale = nullptr;
    UpscaleSliderField *m_strengthField = nullptr;
    QCheckBox *m_osdDetection;
    QCheckBox *m_osdSummary;
    QCheckBox *m_osdStatistics;
    QCheckBox *m_osdDeveloper;
    QCheckBox *m_osdEveryFullScreen;
    QComboBox *m_osdAnnouncementPosition;
    QComboBox *m_osdStatisticsPosition;
    QComboBox *m_osdDeveloperPosition;
    QSpinBox *m_osdTimeout;
    UpscaleMethodControls *m_methods = nullptr;
    UpscaleApplicationEditor *m_editor = nullptr;
    QLabel *m_applications;
    QPushButton *m_resetApplications;
    // A report is being assembled: the window is being picked or asked about.
    bool m_reporting = false;
    QLabel *m_build;
};

} // namespace KWin
