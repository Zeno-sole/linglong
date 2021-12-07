/*
 * Copyright (c) 2020-2021. Uniontech Software Ltd. All rights reserved.
 *
 * Author:     huqinghong@uniontech.com
 *
 * Maintainer: huqinghong@uniontech.com
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <pwd.h>
#include <sys/types.h>

#include "module/repo/repohelper_factory.h"
#include "module/repo/ostree_repohelper.h"
#include "module/util/app_status.h"
#include "module/util/appinfo_cache.h"
#include "module/util/httpclient.h"
#include "module/util/package_manager_param.h"

#include "package_manager_impl.h"
#include "dbus_retcode.h"

using linglong::dbus::RetCode;
using namespace linglong::Status;

const QString kAppInstallPath = "/deepin/linglong/layers/";
const QString kLocalRepoPath = "/deepin/linglong/repo";

/*
 * 查询系统架构
 *
 * @return QString: 系统架构字符串
 */
QString PackageManagerImpl::getHostArch()
{
    // other CpuArchitecture ie i386 i486 to do fix
    const QString arch = QSysInfo::currentCpuArchitecture();
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (arch.startsWith("x86_64", cs)) {
        return "x86_64";
    }
    if (arch.startsWith("arm", cs)) {
        return "arm64";
    }
    if (arch.startsWith("mips", cs)) {
        return "mips64";
    }
    return "unknown";
}

/*
 * 查询当前登陆用户名
 *
 * @return QString: 当前登陆用户名
 */
QString PackageManagerImpl::getUserName()
{
    // QString userPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    // QString userName = userPath.section("/", -1, -1);
    // return userName;
    uid_t uid = geteuid();
    struct passwd *user = getpwuid(uid);
    QString userName = "";
    if (user && user->pw_name) {
        userName = QString(QLatin1String(user->pw_name));
    } else {
        qInfo() << "getUserName err";
    }
    return userName;
}

/*
 * 将json字符串转化为软件包数据
 *
 * @param jsonString: 软件包对应的json字符串
 * @param appList: 转换结果
 * @param err: 错误信息
 *
 * @return bool: true:成功 false:失败
 */
bool PackageManagerImpl::loadAppInfo(const QString &jsonString, QList<AppMetaInfo *> &appList, QString &err)
{
    QJsonParseError parseJsonErr;
    QJsonDocument document = QJsonDocument::fromJson(jsonString.toUtf8(), &parseJsonErr);
    if (!(parseJsonErr.error == QJsonParseError::NoError)) {
        err = "parse json data err";
        return false;
    }

    QJsonObject jsonObject = document.object();
    if (jsonObject.size() == 0) {
        err = "receive data is empty";
        return false;
    }

    if (!jsonObject.contains("code") || !jsonObject.contains("data")) {
        err = "receive data format err";
        return false;
    }

    int code = jsonObject["code"].toInt();
    if (code != 0) {
        err = "app not found in repo";
        qCritical().noquote() << jsonString;
        return false;
    }

    QJsonValue arrayValue = jsonObject.value(QStringLiteral("data"));
    if (!arrayValue.isArray()) {
        err = "jsonString from server data format is not array";
        qCritical().noquote() << jsonString;
        return false;
    }

    // 多个结果以json 数组形式返回
    QJsonArray arr = arrayValue.toArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject dataObj = arr.at(i).toObject();
        const QString jsonString = QString(QJsonDocument(dataObj).toJson(QJsonDocument::Compact));
        // qInfo().noquote() << jsonString;
        auto appItem = linglong::util::loadJSONString<AppMetaInfo>(jsonString);
        appList.push_back(appItem);
    }
    return true;
}

/*
 * 从服务器查询指定包名/版本/架构的软件包数据
 *
 * @param pkgName: 软件包包名
 * @param pkgVer: 软件包版本号
 * @param pkgArch: 软件包对应的架构
 * @param appData: 查询结果
 * @param err: 错误信息
 *
 * @return bool: true:成功 false:失败
 */
bool PackageManagerImpl::getAppInfofromServer(const QString &pkgName, const QString &pkgVer, const QString &pkgArch,
                                              QString &appData, QString &err)
{
    linglong::util::HttpClient *httpClient = linglong::util::HttpClient::getInstance();
    bool ret = httpClient->queryRemote(pkgName, pkgVer, pkgArch, appData);
    httpClient->release();
    if (!ret) {
        err = "getAppInfofromServer err";
        qCritical() << err;
        return false;
    }

    qDebug().noquote() << appData;
    return true;
}

/*
 * 将在线包数据部分签出到指定目录
 *
 * @param pkgName: 软件包包名
 * @param pkgVer: 软件包版本号
 * @param pkgArch: 软件包对应的架构
 * @param dstPath: 在线包数据部分存储路径
 * @param err: 错误信息
 *
 * @return bool: true:成功 false:失败
 */
bool PackageManagerImpl::downloadAppData(const QString &pkgName, const QString &pkgVer, const QString &pkgArch,
                                         const QString &dstPath, QString &err)
{
    // linglong::OstreeRepoHelper repo;
    bool ret = G_OSTREE_REPOHELPER->ensureRepoEnv(kLocalRepoPath, err);
    // bool ret = repo->ensureRepoEnv(repoPath, err);
    if (!ret) {
        qInfo() << err;
        return false;
    }
    QVector<QString> qrepoList;
    ret = G_OSTREE_REPOHELPER->getRemoteRepoList(kLocalRepoPath, qrepoList, err);
    if (!ret) {
        qInfo() << err;
        return false;
    } else {
        for (auto iter = qrepoList.begin(); iter != qrepoList.end(); ++iter) {
            qInfo() << "downloadAppData remote reponame:" << *iter;
        }
    }

    // ref format --> app/org.deepin.calculator/x86_64/1.2.2
    // QString matchRef = QString("app/%1/%2/%3").arg(pkgName).arg(pkgArch).arg(pkgVer);
    // QString pkgName = "us.zoom.Zoom";
    QString matchRef = "";
    ret = G_REPOHELPER->queryMatchRefs(kLocalRepoPath, qrepoList[0], pkgName, pkgVer, pkgArch, matchRef, err);
    if (!ret) {
        qInfo() << err;
        return false;
    } else {
        qInfo() << "downloadAppData ref:" << matchRef;
    }

    // ret = repo.repoPull(repoPath, qrepoList[0], pkgName, err);
    ret = G_REPOHELPER->repoPullbyCmd(kLocalRepoPath, qrepoList[0], matchRef, err);
    if (!ret) {
        qInfo() << err;
        return false;
    }
    // checkout 目录
    // const QString dstPath = repoPath + "/AppData";
    ret = G_OSTREE_REPOHELPER->checkOutAppData(kLocalRepoPath, qrepoList[0], matchRef, dstPath, err);
    if (!ret) {
        qInfo() << err;
        return false;
    }
    qInfo() << "downloadAppData success, path:" << dstPath;

    return ret;
}

/*!
 * 下载软件包
 * @param packageIDList
 */
RetMessageList PackageManagerImpl::Download(const QStringList &packageIDList, const QString &savePath)
{
    return {};
}

/*
 * 安装应用runtime
 *
 * @param runtimeID: runtime对应的appID
 * @param runtimeVer: runtime版本号
 * @param runtimeArch: runtime对应的架构
 * @param err: 错误信息
 *
 * @return bool: true:成功 false:失败
 */
bool PackageManagerImpl::installRuntime(const QString &runtimeID, const QString &runtimeVer, const QString &runtimeArch,
                                        QString &err)
{
    QList<AppMetaInfo *> appList;
    QString appData = "";

    bool ret = getAppInfofromServer(runtimeID, runtimeVer, runtimeArch, appData, err);
    if (!ret) {
        return false;
    }
    ret = loadAppInfo(appData, appList, err);
    if (!ret) {
        qCritical() << err;
        return false;
    }
    // app runtime 只能匹配一个
    if (appList.size() != 1) {
        err = "app:" + runtimeID + ", version:" + runtimeVer + " not found in repo";
        return false;
    }

    auto pkgInfo = appList.at(0);
    const QString savePath = kAppInstallPath + runtimeID + "/" + runtimeVer + "/" + runtimeArch;
    // 创建路径
    linglong::util::createDir(savePath);
    ret = downloadAppData(runtimeID, runtimeVer, runtimeArch, savePath, err);
    if (!ret) {
        err = "installRuntime download runtime data err";
        return false;
    }

    // 更新本地数据库文件
    QString userName = getUserName();
    pkgInfo->kind = "runtime";
    insertAppRecord(pkgInfo, "user", userName);
    return true;
}

/*
 * 检查应用runtime安装状态
 *
 * @param runtime: 应用runtime字符串
 * @param err: 错误信息
 *
 * @return bool: true:安装成功或已安装返回true false:安装失败
 */
bool PackageManagerImpl::checkAppRuntime(const QString &runtime, QString &err)
{
    // runtime ref in repo com.deepin.Runtime/20/x86_64
    QStringList runtimeInfo = runtime.split("/");
    if (runtimeInfo.size() != 3) {
        err = "app runtime:" + runtime + " runtime format err";
        return false;
    }
    const QString runtimeID = runtimeInfo.at(0);
    const QString runtimeVer = runtimeInfo.at(1);
    const QString runtimeArch = runtimeInfo.at(2);

    bool ret = true;
    // 判断app依赖的runtime是否安装
    QString userName = getUserName();
    if (!getAppInstalledStatus(runtimeID, runtimeVer, "", userName)) {
        ret = installRuntime(runtimeID, runtimeVer, runtimeArch, err);
    }
    return ret;
}

/*!
 * 在线安装软件包
 * @param packageIDList
 */
RetMessageList PackageManagerImpl::Install(const QStringList &packageIDList, const ParamStringMap &paramMap)
{
    RetMessageList retMsg;
    bool ret = false;
    auto info = QPointer<RetMessage>(new RetMessage);
    QString pkgName = packageIDList.at(0);
    if (pkgName.isNull() || pkgName.isEmpty()) {
        qInfo() << "package name err";
        info->setcode(RetCode(RetCode::user_input_param_err));
        info->setmessage("package name err");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    QString err = "";

    // const QString arch = "x86_64";
    const QString arch = getHostArch();
    if (arch == "unknown") {
        qInfo() << "the host arch is not recognized";
        info->setcode(RetCode(RetCode::host_arch_not_recognized));
        info->setmessage("the host arch is not recognized");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    // 获取版本信息
    QString version = "";
    if (!paramMap.empty() && paramMap.contains(linglong::util::KEY_VERSION)) {
        version = paramMap[linglong::util::KEY_VERSION];
    }

    QString userName = getUserName();
    QString appData = "";
    // 安装不查缓存
    ret = getAppInfofromServer(pkgName, version, arch, appData, err);
    if (!ret) {
        qCritical() << err;
        info->setcode(RetCode(RetCode::pkg_install_failed));
        info->setmessage(err);
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }
    QList<AppMetaInfo *> appList;
    ret = loadAppInfo(appData, appList, err);
    if (!ret) {
        qCritical() << err;
        info->setcode(RetCode(RetCode::pkg_install_failed));
        info->setmessage(err);
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }
    if (appList.size() != 1) {
        err = "app:" + pkgName + ", version:" + version + " not found in repo";
        qCritical() << err;
        info->setcode(RetCode(RetCode::pkg_install_failed));
        info->setmessage(err);
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    auto appInfo = appList.at(0);
    // 判断对应版本的应用是否已安装
    if (getAppInstalledStatus(pkgName, appInfo->version, "", userName)) {
        qCritical() << pkgName << ", version: " << appInfo->version << " already installed";
        info->setcode(RetCode(RetCode::pkg_already_installed));
        info->setmessage(pkgName + ", version: " + appInfo->version + " already installed");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    // 检查软件包依赖的runtime安装状态
    ret = checkAppRuntime(appInfo->runtime, err);
    if (!ret) {
        qCritical() << err;
        info->setcode(RetCode(RetCode::install_runtime_failed));
        info->setmessage(err);
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    // 下载在线包数据到目标目录 安装完成
    // QString pkgName = "org.deepin.calculator";
    const QString savePath = kAppInstallPath + appInfo->appId + "/" + appInfo->version + "/" + appInfo->arch;
    ret = downloadAppData(appInfo->appId, appInfo->version, appInfo->arch, savePath, err);
    if (!ret) {
        qInfo() << err;
        info->setcode(RetCode(RetCode::load_pkg_data_failed));
        info->setmessage(err);
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    //链接应用配置文件到系统配置目录
    if (linglong::util::dirExists(savePath + "/outputs/share")) {
        const QString appEntriesDirPath = savePath + "/outputs/share";
        linglong::util::linkDirFiles(appEntriesDirPath, sysLinglongInstalltions);
    } else {
        const QString appEntriesDirPath = savePath + "/entries";
        linglong::util::linkDirFiles(appEntriesDirPath, sysLinglongInstalltions);
    }
    // 更新本地数据库文件
    appInfo->kind = "app";
    insertAppRecord(appInfo, "user", userName);

    info->setcode(RetCode(RetCode::pkg_install_success));
    info->setmessage("install " + pkgName + ", version:" + appInfo->version + " success");
    info->setstate(true);
    retMsg.push_back(info);
    return retMsg;
}

/*!
 * 查询软件包
 * @param packageIDList: 软件包的appid
 *
 * @return PKGInfoList 查询结果列表
 */
PKGInfoList PackageManagerImpl::Query(const QStringList &packageIDList, const ParamStringMap &paramMap)
{
    const QString pkgName = packageIDList.at(0);
    if (pkgName.isNull() || pkgName.isEmpty()) {
        qInfo() << "package name err";
        return {};
    }
    if (pkgName == "installed") {
        return queryAllInstalledApp();
    }
    PKGInfoList pkglist;
    // 查找单个软件包 优先从本地数据库文件中查找
    QString arch = getHostArch();
    if (arch == "unknown") {
        qInfo() << "the host arch is not recognized";
        return pkglist;
    }

    QString userName = getUserName();
    bool ret = getInstalledAppInfo(pkgName, "", arch, userName, pkglist);

    // 目标软件包 已安装则终止查找
    qInfo() << "PackageManager::Query called, ret:" << ret;
    if (ret) {
        return pkglist;
    }

    QString err = "";
    QString appData = "";
    QList<AppMetaInfo *> appList;
    int status = queryLocalCache(pkgName, appData);
    bool fromServer = false;
    // 缓存查不到从服务器查
    if (status != StatusCode::SUCCESS) {
        ret = getAppInfofromServer(pkgName, "", arch, appData, err);
        if (!ret) {
            qCritical() << err;
            return pkglist;
        }
        fromServer = true;
    }
    ret = loadAppInfo(appData, appList, err);
    if (!ret) {
        qCritical() << err;
        return pkglist;
    }
    // 若从服务器查询得到正确的数据则更新缓存
    if (fromServer) {
        updateCache(pkgName, appData);
    }
    for (auto it : appList) {
        auto info = QPointer<PKGInfo>(new PKGInfo);
        info->appid = it->appId;
        info->appname = it->name;
        info->version = it->version;
        info->arch = it->arch;
        info->description = it->description;
        pkglist.push_back(info);
    }
    return pkglist;
}

/*
 * 卸载软件包
 *
 * @param packageIDList: 软件包的appid
 *
 * @return RetMessageList 卸载结果信息
 */
RetMessageList PackageManagerImpl::Uninstall(const QStringList &packageIDList, const ParamStringMap &paramMap)
{
    RetMessageList retMsg;
    auto info = QPointer<RetMessage>(new RetMessage);
    const QString pkgName = packageIDList.at(0);

    // 获取版本信息
    QString version = "";
    if (!paramMap.empty() && paramMap.contains(linglong::util::KEY_VERSION)) {
        version = paramMap[linglong::util::KEY_VERSION];
    }

    // 判断是否已安装
    QString userName = getUserName();
    if (!getAppInstalledStatus(pkgName, version, "", userName)) {
        qCritical() << pkgName << " not installed";
        info->setcode(RetCode(RetCode::pkg_not_installed));
        info->setmessage(pkgName + " not installed");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }
    QString err = "";
    PKGInfoList pkglist;
    // 根据已安装文件查询已经安装软件包信息
    QString arch = getHostArch();
    getInstalledAppInfo(pkgName, version, arch, userName, pkglist);

    auto it = pkglist.at(0);
    if (pkglist.size() > 0) {
        const QString installPath = kAppInstallPath + it->appid + "/" + it->version;
        // 删掉安装配置链接文件
        if (linglong::util::dirExists(installPath + "/" + arch + "/outputs/share")) {
            const QString appEntriesDirPath = installPath + "/" + arch + "/outputs/share";
            linglong::util::removeDstDirLinkFiles(appEntriesDirPath, sysLinglongInstalltions);
        } else {
            const QString appEntriesDirPath = installPath + "/" + arch + "/entries";
            linglong::util::removeDstDirLinkFiles(appEntriesDirPath, sysLinglongInstalltions);
        }
        linglong::util::removeDir(installPath);
        qInfo() << "Uninstall del dir:" << installPath;
    }

    // 更新本地repo仓库
    bool ret = G_OSTREE_REPOHELPER->ensureRepoEnv(kLocalRepoPath, err);
    if (!ret) {
        qCritical() << err;
        info->setcode(RetCode(RetCode::pkg_uninstall_failed));
        info->setmessage("uninstall local repo not exist");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }
    // 应从安装数据库获取应用所属仓库信息 to do fix
    QVector<QString> qrepoList;
    ret = G_OSTREE_REPOHELPER->getRemoteRepoList(kLocalRepoPath, qrepoList, err);
    if (!ret) {
        qCritical() << err;
        info->setcode(RetCode(RetCode::pkg_uninstall_failed));
        info->setmessage("uninstall remote repo not exist");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }
    // ref format --> app/org.deepin.calculator/x86_64/1.2.2
    QString matchRef = QString("app/%1/%2/%3").arg(it->appid).arg(arch).arg(it->version);
    qInfo() << "Uninstall app ref:" << matchRef;
    ret = G_OSTREE_REPOHELPER->repoDeleteDatabyRef(kLocalRepoPath, qrepoList[0], matchRef, err);
    if (!ret) {
        qCritical() << err;
        info->setcode(RetCode(RetCode::pkg_uninstall_failed));
        info->setmessage("uninstall " + pkgName + ",version:" + it->version + " failed");
        info->setstate(false);
        retMsg.push_back(info);
        return retMsg;
    }

    // 更新安装数据库
    deleteAppRecord(pkgName, it->version, "", userName);
    info->setcode(RetCode(RetCode::pkg_uninstall_success));
    info->setmessage("uninstall " + pkgName + ",version:" + it->version + " success");
    info->setstate(true);
    retMsg.push_back(info);
    return retMsg;
}