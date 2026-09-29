/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The report a person sends with a new entry: what it states, and what it
// never carries about the person who sends it.

#include "application.h"
#include "submission.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace KWin;

// A Steam game in someone's home, drawing a smaller buffer on Wayland, with
// what the effect does not send beside it, as a leak would carry it.
static QVariantMap waylandGame()
{
    return {
        {QStringLiteral("executable"), QStringLiteral("/home/anna/.local/share/Steam/steamapps/common/Racer/bin/racer")},
        {QStringLiteral("windowClass"), QStringLiteral("racer")},
        {QStringLiteral("instance"), QStringLiteral("racer")},
        {QStringLiteral("x11"), false},
        {QStringLiteral("presentation"), QStringLiteral("MethodWaylandFullScreen")},
        {QStringLiteral("method"), QStringLiteral("AdvertisedMode")},
        {QStringLiteral("advertised"), QStringLiteral("2560x1440")},
        {QStringLiteral("supplied"), QStringLiteral("2560x1440")},
        {QStringLiteral("destination"), QStringLiteral("3840x2160")},
        {QStringLiteral("outputScale"), 1.5},
        {QStringLiteral("build"), QStringLiteral("0.3.0")},
        {QStringLiteral("kwin"), QStringLiteral("6.3.6")},
        {QStringLiteral("graphics"), QStringLiteral("OpenGL")},
        {QStringLiteral("renderer"), QStringLiteral("llvmpipe")},
        {QStringLiteral("driver"), QStringLiteral("4.5 Mesa 25.0.7")},
        {QStringLiteral("caption"), QStringLiteral("Racer - Anna's save 3")},
        {QStringLiteral("environment"), QStringLiteral("HOME=/home/anna")},
    };
}

class SubmissionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void statesTheEntryAsObserved();
    void carriesNothingAboutThePerson();
    void readsAsAnEntry();
    void saysWhereTheProgramIsNotNamed();
    void describesAnX11Request();
};

void SubmissionTest::statesTheEntryAsObserved()
{
    const QString report = upscaleSubmissionReport(waylandGame(), QStringLiteral("Debian GNU/Linux 13 (trixie)"));
    for (const char *line : {"Executable=.*/Racer/bin/racer", "ExecutableMatch=RegularExpression", "WindowClass=racer",
                             "Instance=racer", "MethodWaylandFullScreen=AdvertisedMode", "# Window system: Wayland",
                             "# Method: AdvertisedMode; told 2560 x 1440",
                             "# Buffer: 2560 x 1440 supplied, for an output of 3840 x 2160 pixels at scale 1.5",
                             "# Effect: 0.3.0; KWin 6.3.6; OpenGL", "# Graphics: llvmpipe; 4.5 Mesa 25.0.7",
                             "# System: Debian GNU/Linux 13 (trixie)", "# The image covers the screen: <yes or no>"}) {
        QVERIFY2(report.contains(QLatin1String(line) + QLatin1Char('\n')), line);
    }
}

void SubmissionTest::carriesNothingAboutThePerson()
{
    const QString report = upscaleSubmissionReport(waylandGame(), QStringLiteral("Debian"));
    // No absolute path, which says where somebody keeps their games, no
    // window title, which carries save names, and no environment.
    QVERIFY(!report.contains(QLatin1String("/home")));
    QVERIFY(!report.contains(QLatin1String("anna"), Qt::CaseInsensitive));
    QVERIFY(!report.contains(QLatin1String("save 3")));
    QVERIFY(!report.contains(QLatin1String("HOME=")));
}

void SubmissionTest::readsAsAnEntry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("report.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(upscaleSubmissionReport(waylandGame(), QStringLiteral("Debian")).toUtf8()) > 0);
    file.close();
    const std::vector<UpscaleApplication> read = upscaleReadApplicationFile(path);
    QCOMPARE(read.size(), std::size_t(1));
    QCOMPARE(read.front().executable, QStringLiteral(".*/Racer/bin/racer"));
    QCOMPARE(read.front().windowClass, QStringLiteral("racer"));
    QCOMPARE(read.front().methods[std::size_t(UpscalePresentation::WaylandFullScreen)], std::optional(UpscaleMethod::AdvertisedMode));
}

void SubmissionTest::saysWhereTheProgramIsNotNamed()
{
    QVariantMap facts = waylandGame();
    facts.insert(QStringLiteral("executable"), QString());
    QString report = upscaleSubmissionReport(facts, QStringLiteral("Debian"));
    QVERIFY(report.contains(QLatin1String("# Program: not known for this window.\n")));
    QVERIFY(!report.contains(QLatin1String("Executable=")));
    // Proton's loader runs every game, so its path would name all of them.
    facts.insert(QStringLiteral("executable"), QStringLiteral("/home/anna/.steam/steam/steamapps/common/Proton/files/bin/wine64-preloader"));
    report = upscaleSubmissionReport(facts, QStringLiteral("Debian"));
    QVERIFY(report.contains(QLatin1String("# Program: a runtime many programs share")));
    QVERIFY(!report.contains(QLatin1String("Executable=")));
    QVERIFY(!report.contains(QLatin1String("/home")));
}

void SubmissionTest::describesAnX11Request()
{
    QVariantMap facts = waylandGame();
    facts.insert(QStringLiteral("x11"), true);
    facts.insert(QStringLiteral("presentation"), QStringLiteral("MethodX11FullScreen"));
    facts.insert(QStringLiteral("method"), QStringLiteral("X11Resize"));
    facts.remove(QStringLiteral("advertised"));
    facts.insert(QStringLiteral("requested"), QStringLiteral("1920x1080"));
    facts.insert(QStringLiteral("requestFailure"), QStringLiteral("The requested X11 mode is unavailable on this output."));
    const QString report = upscaleSubmissionReport(facts, QStringLiteral("Debian"));
    QVERIFY(report.contains(QLatin1String("MethodX11FullScreen=X11Resize\n")));
    QVERIFY(report.contains(QLatin1String("# Window system: X11, through Xwayland\n")));
    QVERIFY(report.contains(QLatin1String("# Method: X11Resize; asked for 1920 x 1080; failed: The requested X11 mode is unavailable on this output.\n")));
}

QTEST_MAIN(SubmissionTest)

#include "submission_test.moc"
