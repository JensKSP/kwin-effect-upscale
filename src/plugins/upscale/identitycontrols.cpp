/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "identitycontrols.h"

#include "application.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace KWin
{

UpscaleIdentityControls::UpscaleIdentityControls(QObject *parent)
    : QObject(parent)
{
}

void UpscaleIdentityControls::build(QFormLayout *form, QWidget *parent)
{
    const std::array<QString, 3> labels{i18n("Program:"), i18n("Window class:"), i18n("Window instance:")};
    // The names the rest of the settings page's controls carry, so that the
    // tests reach these the way they reach everything else on it.
    const std::array<QString, 3> names{QStringLiteral("applicationProgram"), QStringLiteral("applicationWindowClass"),
                                       QStringLiteral("applicationInstance")};
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        Field &field = m_fields[index];
        field.value = new QLineEdit(parent);
        field.value->setPlaceholderText(i18n("Any"));
        field.value->setObjectName(names[index]);
        field.match = new QComboBox(parent);
        // In UpscaleStringMatch's order, so the index is the value.
        field.match->addItems({i18n("Exact match"), i18n("Substring match"), i18n("Regular expression")});
        field.match->setObjectName(names[index] + QStringLiteral("Match"));
        connect(field.value, &QLineEdit::textEdited, this, &UpscaleIdentityControls::changed);
        connect(field.match, &QComboBox::activated, this, &UpscaleIdentityControls::changed);
        auto *row = new QHBoxLayout;
        row->addWidget(field.value, 1);
        row->addWidget(field.match);
        form->addRow(labels[index], row);
    }
    m_fields[0].value->setToolTip(i18n("The program's full path. As a regular expression, .*/supertuxkart matches "
                                       "that program in any folder."));
    m_matches = new QLabel(parent);
    m_matches->setObjectName(QStringLiteral("applicationMatches"));
    m_matches->setWordWrap(true);
    m_matches->setTextFormat(Qt::PlainText);
    m_matches->hide();
    form->addRow(QString(), m_matches);
    m_asking.setSingleShot(true);
    m_asking.setInterval(300);
    connect(&m_asking, &QTimer::timeout, this, &UpscaleIdentityControls::askForMatches);
    connect(this, &UpscaleIdentityControls::changed, &m_asking, qOverload<>(&QTimer::start));
}

void UpscaleIdentityControls::show(const UpscaleApplication &application)
{
    const std::array<std::pair<QString, UpscaleStringMatch>, 3> values{{
        {application.executable, application.executableMatch},
        {application.windowClass, application.windowClassMatch},
        {application.instance, application.instanceMatch},
    }};
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        m_fields[index].value->setText(values[index].first);
        m_fields[index].match->setCurrentIndex(int(values[index].second));
    }
    m_asking.start();
}

UpscaleStringMatch UpscaleIdentityControls::matchOf(std::size_t field) const
{
    // The list's order, looked up rather than cast, so that no index can
    // name a match type that does not exist.
    static constexpr std::array<UpscaleStringMatch, 3> s_matches{
        UpscaleStringMatch::Exact,
        UpscaleStringMatch::Substring,
        UpscaleStringMatch::RegularExpression,
    };
    return s_matches[std::size_t(std::clamp(m_fields[field].match->currentIndex(), 0, int(s_matches.size()) - 1))];
}

void UpscaleIdentityControls::store(UpscaleApplication &application) const
{
    application.executable = m_fields[0].value->text();
    application.executableMatch = matchOf(0);
    application.windowClass = m_fields[1].value->text();
    application.windowClassMatch = matchOf(1);
    application.instance = m_fields[2].value->text();
    application.instanceMatch = matchOf(2);
}

QVariantMap UpscaleIdentityControls::entry() const
{
    const std::array<QString, 3> keys{QStringLiteral("Executable"), QStringLiteral("WindowClass"),
                                      QStringLiteral("Instance")};
    QVariantMap fields;
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        fields.insert(keys[index], m_fields[index].value->text());
        fields.insert(keys[index] + QStringLiteral("Match"), upscaleStringMatchKey(matchOf(index)));
    }
    return fields;
}

void UpscaleIdentityControls::askForMatches()
{
    if (m_query) {
        m_asking.start();
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                                                          QStringLiteral("org.kde.KWin.Effect.Upscale1"),
                                                          QStringLiteral("windowsMatching"));
    message << entry();
    m_query = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(m_query.data(), &QDBusPendingCallWatcher::finished, this, [this]() {
        const QDBusPendingReply<QStringList> reply = *m_query;
        m_query->deleteLater();
        // No answer is not "no window": without the effect there is nobody to
        // ask, and saying nothing is the honest report of that.
        if (!reply.isValid() || !m_fields[0].value->isEnabled()) {
            m_matches->hide();
            return;
        }
        const QStringList captions = reply.value();
        m_matches->setText(captions.isEmpty()
                               ? i18n("No open window matches.")
                               : i18np("Matches the open window “%2”.", "Matches %1 open windows: %2", captions.size(),
                                       captions.join(i18nc("Between window titles", ", "))));
        m_matches->show();
    });
}

void UpscaleIdentityControls::setEnabled(bool enabled)
{
    for (const Field &field : m_fields) {
        field.value->setEnabled(enabled);
        field.match->setEnabled(enabled);
    }
    if (!enabled) {
        m_asking.stop();
        m_matches->hide();
    }
}

bool upscaleIdentifiesOneProgram(const QString &executable)
{
    // By file name, which is what these runtimes share wherever they are
    // installed. Wine's loaders and preloaders, and the interpreters games
    // are commonly written for.
    static const QRegularExpression s_shared(QStringLiteral(
        "^(wine|wine64|wine-preloader|wine64-preloader|python[0-9.]*|java|mono|dotnet|love)$"));
    const QString file = executable.section(QLatin1Char('/'), -1);
    return !file.isEmpty() && !s_shared.match(file).hasMatch();
}

} // namespace KWin
