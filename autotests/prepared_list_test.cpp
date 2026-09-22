/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "preparedlist.h"
#include "settings_fixture.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QLabel>
#include <QPushButton>
#include <QTest>

// One entry of Prepared(), as a helper sends it: a(ssii).
struct TestProgram
{
    QString id;
    QString title;
    int width = 0;
    int height = 0;
};
Q_DECLARE_METATYPE(TestProgram)

QDBusArgument &operator<<(QDBusArgument &argument, const TestProgram &program)
{
    argument.beginStructure();
    argument << program.id << program.title << program.width << program.height;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TestProgram &program)
{
    argument.beginStructure();
    argument >> program.id >> program.title >> program.width >> program.height;
    argument.endStructure();
    return argument;
}

// Stands in for a helper answering org.kde.KWin.Upscale.Helper1.
class TestHelper : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Upscale.Helper1")

public:
    QList<TestProgram> programs;
    QStringList resets;

public Q_SLOTS:
    Q_SCRIPTABLE QList<TestProgram> Prepared()
    {
        return programs;
    }
    Q_SCRIPTABLE bool Reset(const QString &id)
    {
        resets.append(id);
        programs.removeIf([&id](const TestProgram &program) {
            return program.id == id;
        });
        return true;
    }
};

class PreparedListTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void showsWhatAHelperPreparedAndUndoesIt();
};

void PreparedListTest::showsWhatAHelperPreparedAndUndoesIt()
{
    qDBusRegisterMetaType<TestProgram>();
    qDBusRegisterMetaType<QList<TestProgram>>();
    QWidget host;
    auto list = new KWin::UpscalePreparedList(&host);
    host.show();

    // Nobody answers without a helper, and the page shows nothing of it.
    list->refresh();
    QTest::qWait(600);
    QVERIFY(list->isHidden());

    QDBusConnection bus = QDBusConnection::sessionBus();
    TestHelper helper;
    helper.programs = {{QStringLiteral("803-1a2b"), QStringLiteral("Wreckfest"), 2560, 1440},
                       {QStringLiteral("803-1a2c"), QStringLiteral("Another Game"), 1920, 1080}};
    QVERIFY(bus.registerObject(QStringLiteral("/Helper"), &helper, QDBusConnection::ExportScriptableSlots));
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin.Upscale.Helper")));
    list->refresh();
    QTRY_VERIFY(!list->isHidden());
    QCOMPARE(list->findChildren<QPushButton *>().size(), 2);
    QStringList labels;
    for (const QLabel *label : list->findChildren<QLabel *>()) {
        labels.append(label->text());
    }
    QVERIFY(labels.contains(QStringLiteral("Wreckfest, rendered at 2560 × 1440")));

    // Undoing one asks the helper and shows what is left.
    list->findChildren<QPushButton *>().first()->click();
    QTRY_COMPARE(list->findChildren<QPushButton *>().size(), 1);
    QCOMPARE(helper.resets, QStringList{QStringLiteral("803-1a2b")});

    list->findChildren<QPushButton *>().first()->click();
    QTRY_VERIFY(list->isHidden());

    bus.unregisterObject(QStringLiteral("/Helper"));
    QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin.Upscale.Helper")));
}

int main(int argc, char **argv)
{
    // A private session bus, where the test's helper can register.
    return runSettingsTest<PreparedListTest>(argc, argv);
}

#include "prepared_list_test.moc"
