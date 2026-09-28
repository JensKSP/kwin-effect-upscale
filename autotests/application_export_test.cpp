/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"
#include "pattern.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace KWin;

class ApplicationExportTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void preservesShippedMethods();
    void carriesAProgramToAnotherUser();
};

void ApplicationExportTest::preservesShippedMethods()
{
    QFile::remove(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    upscaleReloadApplications();
    const std::vector<UpscaleApplication> originals = upscaleApplications();
    QVERIFY(!originals.empty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.ini"));
    QVERIFY(upscaleWriteApplicationFile(originals, path));
    const std::vector<UpscaleApplication> imported = upscaleReadApplicationFile(path);
    QCOMPARE(imported.size(), originals.size());
    for (const UpscaleApplication &original : originals) {
        const auto entry = std::ranges::find(imported, original.id, &UpscaleApplication::id);
        QVERIFY(entry != imported.end());
        QCOMPARE(entry->x11ConnectionExecutable, original.x11ConnectionExecutable);
        for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
            QVERIFY2(entry->methods[slot] == original.methods[slot], qPrintable(original.id));
        }
    }
}

// A program as Add from Window stores it, exported from one user's list and
// imported into another's, finds the same game in that user's library.
void ApplicationExportTest::carriesAProgramToAnotherUser()
{
    UpscaleApplication game;
    game.id = QStringLiteral("added");
    game.name = QStringLiteral("Left 4 Dead 2");
    game.executable = QStringLiteral(".*/Left 4 Dead 2/hl2_linux");
    game.executableMatch = UpscaleStringMatch::RegularExpression;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.ini"));
    QVERIFY(upscaleWriteApplicationFile({game}, path));
    QFile exported(path);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(!exported.readAll().contains("/home/"));
    const std::vector<UpscaleApplication> imported = upscaleReadApplicationFile(path);
    QCOMPARE(imported.size(), std::size_t(1));
    const UpscalePattern program(imported.front().executable, imported.front().executableMatch);
    QVERIFY(program.matches(QStringLiteral("/home/kim/.steam/debian-installation/steamapps/common/Left 4 Dead 2/hl2_linux")));
}

int runApplicationExportTest(int argc, char *argv[])
{
    ApplicationExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "application_export_test.moc"
