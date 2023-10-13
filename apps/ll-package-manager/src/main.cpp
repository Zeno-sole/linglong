/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "linglong/adaptors/job_manager/job_manager1.h"
#include "linglong/adaptors/package_manager/package_manager1.h"
#include "linglong/dbus_ipc/workaround.h"
#include "linglong/package_manager/package_manager.h"
#include "linglong/utils/dbus/register.h"
#include "linglong/utils/finally/finally.h"
#include "linglong/utils/global/initialize.h"

#include <QCoreApplication>

auto main(int argc, char *argv[]) -> int
{
    QCoreApplication app(argc, argv);

    using namespace linglong::utils::global;
    using linglong::utils::dbus::registerDBusObject;
    using linglong::utils::dbus::registerDBusService;
    using linglong::utils::dbus::unregisterDBusObject;
    using linglong::utils::dbus::unregisterDBusService;
    using linglong::utils::finally::finally;

    applicationInitializte();

    registerDBusParam();

    QDBusConnection conn = QDBusConnection::systemBus();
    linglong::service::PackageManager packageManager;
    linglong::adaptors::package_manger::PackageManager1 packageManagerAdaptor(&packageManager);
    auto result = registerDBusObject(conn, "/org/deepin/linglong/PackageManager", &packageManager);
    auto _ = finally([&conn] {
        unregisterDBusObject(conn,
                             // FIXME: use cmake option
                             "/org/deepin/linglong/PackageManager");
    });
    if (!result.has_value()) {
        qCritical().noquote() << "Launching failed:" << Qt::endl << result.error()->message();
        return -1;
    }

    linglong::adaptors::job_manger::JobManager1 jobMangerAdaptor(
            linglong::job_manager::JobManager::instance());
    result = registerDBusObject(conn,
                                // FIXME: use cmake option
                                "/org/deepin/linglong/JobManager",
                                linglong::job_manager::JobManager::instance());
    auto __ = finally([&conn] {
        unregisterDBusObject(conn,
                             // FIXME: use cmake option
                             "/org/deepin/linglong/JobManager");
    });
    if (!result.has_value()) {
        qCritical().noquote() << "Launching failed:" << Qt::endl << result.error()->message();
        return -1;
    }

    result = registerDBusService(conn,
                                 // FIXME: use cmake option
                                 "org.deepin.linglong.PackageManager");
    auto ___ = finally([&conn] {
        auto result = unregisterDBusService(conn,
                                            // FIXME: use cmake option
                                            "org.deepin.linglong.PackageManager");
        if (!result.has_value()) {
            qWarning().noquote() << "During exiting:" << Qt::endl << result.error()->message();
        }
    });
    if (!result.has_value()) {
        qCritical().noquote() << "Launching failed:" << Qt::endl << result.error()->message();
        return -1;
    }

    return QCoreApplication::exec();
}