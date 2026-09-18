/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <KPluginFactory>

#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QPluginLoader>

// Run against installed package paths in a clean distribution container. Loading
// the factories resolves their dependencies without constructing an effect in a
// nonexistent compositor session. GPU/session acceptance remains a separate test.
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3) {
        qCritical() << "Expected the installed effect and configuration module paths";
        return 1;
    }
    for (const QString &path : application.arguments().mid(1)) {
        QPluginLoader loader(path);
        loader.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if (!qobject_cast<KPluginFactory *>(loader.instance())) {
            qCritical() << path << loader.errorString();
            return 1;
        }
        if (loader.metaData().value(QStringLiteral("IID")).toString().isEmpty()) {
            qCritical() << "Missing plugin factory identity:" << path;
            return 1;
        }
        qInfo() << "Loaded installed plugin factory:" << path;
    }
    return 0;
}
