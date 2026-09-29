/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The rule an entry meets before it ships, whoever measured it: the list this
// build installs is held to it, and so is every submitted entry accepted into
// that list. It runs inside application_test.cpp's main.

#include "application.h"
#include "matching.h"

#include <KConfig>
#include <KConfigGroup>

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace KWin;

// What keeps an entry of the file at @p path from shipping, one line each, and
// nothing for a file whose every entry may ship.
static QStringList catalogueProblems(const QString &path)
{
    QStringList problems;
    const KConfig file(path, KConfig::SimpleConfig);
    const QStringList groups = file.groupList().filter(QRegularExpression(QStringLiteral("^Application-")));
    const std::vector<UpscaleApplication> applications = upscaleReadApplicationFile(path);
    // Reading the list drops an entry that would match anything.
    if (applications.size() != std::size_t(groups.size())) {
        problems.append(QStringLiteral("an entry constrains no identity"));
    }
    for (const QString &group : groups) {
        const KConfigGroup entry(&file, group);
        for (std::size_t index = 0; index < upscalePresentationCount; ++index) {
            // A name this build does not know reads as Off, so the entry would
            // ship a method nobody is ever asked for; a known one spells itself
            // back unchanged.
            const QString stated = entry.readEntry(upscalePresentationKey(UpscalePresentation(index)), QString());
            if (!stated.isEmpty() && upscaleMethodKey(upscaleMethodFromKey(stated, UpscaleMethod::Off)) != stated) {
                problems.append(QStringLiteral("%1 states a method this build does not know: %2").arg(group, stated));
            }
        }
        // Reading the list names an entry by its id where it names nothing.
        if (!entry.hasKey("Name")) {
            problems.append(QStringLiteral("%1 names no application").arg(group));
        }
        // A resolution is taste, not a measurement, and would keep the
        // person's own global choice from ever reaching the game.
        if (entry.hasKey("Resolution")) {
            problems.append(QStringLiteral("%1 states a resolution").arg(group));
        }
    }
    // A class that carries the program's version changes with its next
    // release, so it never identifies a program alone.
    static const QRegularExpression versioned(QStringLiteral("\\d+\\.\\d+"));
    int previous = -1;
    for (const UpscaleApplication &application : applications) {
        const QString id = application.id;
        if (application.version.isEmpty()) {
            problems.append(QStringLiteral("%1 states no measured version").arg(id));
        }
        if (application.note.isEmpty()) {
            problems.append(QStringLiteral("%1 has no note for a person").arg(id));
        }
        if (const QString problem = upscaleIdentityProblem(application); !problem.isEmpty()) {
            problems.append(QStringLiteral("%1: %2").arg(id, problem));
        }
        if (application.windowClass.contains(versioned) && application.instance.isEmpty() && application.executable.isEmpty()) {
            problems.append(QStringLiteral("%1 is identified by a class with a version in it alone").arg(id));
        }
        // The list is read in its order, so a repeated one leaves which entry
        // wins to the layout of the file.
        if (application.order <= previous) {
            problems.append(QStringLiteral("%1 repeats an order").arg(id));
        }
        previous = application.order;
    }
    return problems;
}

class CatalogueTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theShippedListMeetsTheRule();
    void anEntryBreakingTheRuleIsNamed_data();
    void anEntryBreakingTheRuleIsNamed();
};

void CatalogueTest::theShippedListMeetsTheRule()
{
    const QStringList problems = catalogueProblems(QStringLiteral(UPSCALE_APPLICATION_DEFAULTS));
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void CatalogueTest::anEntryBreakingTheRuleIsNamed_data()
{
    QTest::addColumn<QByteArray>("entry");
    QTest::addColumn<QString>("problem");
    const QByteArray complete = "Name=Game\nMeasuredVersion=1.0\nNote=Follows resizing.\nInstance=game\nOrder=10\n";
    QTest::newRow("no name") << QByteArray("MeasuredVersion=1.0\nNote=Follows.\nInstance=game\nOrder=10\n") << QStringLiteral("names no application");
    QTest::newRow("no version") << QByteArray("Name=Game\nNote=Follows.\nInstance=game\nOrder=10\n") << QStringLiteral("states no measured version");
    QTest::newRow("no note") << QByteArray("Name=Game\nMeasuredVersion=1.0\nInstance=game\nOrder=10\n") << QStringLiteral("has no note");
    QTest::newRow("no identity") << QByteArray("Name=Game\nMeasuredVersion=1.0\nNote=Follows.\nOrder=10\n") << QStringLiteral("constrains no identity");
    QTest::newRow("unknown method") << complete + "MethodX11FullScreen=Wishful\n"
                                    << QStringLiteral("does not know: Wishful");
    QTest::newRow("resolution") << complete + "Resolution=Quality\n"
                                << QStringLiteral("states a resolution");
    QTest::newRow("versioned class") << QByteArray("Name=Game\nMeasuredVersion=1.0\nNote=Follows.\nWindowClass=Game 1.0\nOrder=10\n")
                                     << QStringLiteral("class with a version in it alone");
    QTest::newRow("repeated order") << complete + "\n[Application-other]\nName=Other\nMeasuredVersion=2\nNote=Follows.\nInstance=other\nOrder=10\n"
                                    << QStringLiteral("repeats an order");
}

void CatalogueTest::anEntryBreakingTheRuleIsNamed()
{
    QFETCH(QByteArray, entry);
    QFETCH(QString, problem);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("kwinupscalerc"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[General]\nFormatVersion=1\n\n[Application-game]\n" + entry) > 0);
    file.close();
    const QStringList problems = catalogueProblems(path);
    QVERIFY2(problems.size() == 1 && problems.first().contains(problem), qPrintable(problems.join(QLatin1Char('\n'))));
}

int runCatalogueTest(int argc, char *argv[])
{
    CatalogueTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "catalogue_test.moc"
