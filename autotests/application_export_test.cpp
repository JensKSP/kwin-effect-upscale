/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"

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
        for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
            QVERIFY2(entry->methods[slot] == original.methods[slot], qPrintable(original.id));
        }
    }
}

int runApplicationExportTest(int argc, char *argv[])
{
    ApplicationExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "application_export_test.moc"
