// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include "usbmanager.h"
#include "nvcomputer.h"
#include "identitymanager.h"
#include "pairedcertificate.h"
#include "usbforwardingtunnel.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
QString component(const QString& relative) {
    for (const char* variable : {"ProgramW6432", "ProgramFiles"}) {
        auto base = qEnvironmentVariable(variable);
        if (base.isEmpty()) continue;
        auto path = QDir(base).filePath(relative);
        if (QFileInfo(path).isFile()) return QDir::toNativeSeparators(path);
    }
    return {};
}
bool validBus(const QString& bus) {
    static const QRegularExpression pattern(QStringLiteral("^[1-9][0-9]*-[1-9][0-9]*$"));
    return bus.size() <= 31 && pattern.match(bus).hasMatch();
}
QString deviceKey(bool fromHost, const QString& bus) { return (fromHost ? "R:" : "L:") + bus; }
}

struct UsbManager::Impl {
    UsbManager* q;
    NvComputer host;
    QNetworkAccessManager network;
    QSslConfiguration ssl;
    QJsonArray local, remote;
    QString message;
    QString localError;
    QByteArray token;
    quint16 tunnelPort = 0;
    bool bidirectional = false;
    int jobs = 0;
    bool ending = false;
    struct Connection {
        UsbForwarding::Tunnel* tunnel = nullptr;
        QProcess* attach = nullptr;
        int hubPort = 0;
        quint16 endpointPort = 0;
        bool closing = false;
        bool ready = false;
        bool fromHost = false;
        QString bus;
    };
    QMap<QString, std::shared_ptr<Connection>> connections;
    explicit Impl(UsbManager* owner, const NvComputer& computer) :
        q(owner), host(computer), ssl(IdentityManager::get()->getSslConfig()) {
        PairedCertificate::enforce(&network, q, host.serverCert);
        QObject::connect(&network, &QNetworkAccessManager::sslErrors, q,
            [this](QNetworkReply* reply, const QList<QSslError>& errors) {
                if (PairedCertificate::canIgnoreErrors(host.serverCert, errors)) reply->ignoreSslErrors(errors);
            });
    }
    void report(const QString& value) { message = value; emit q->changed(); }
    using Result = std::function<void(QByteArray, QString)>;
    void command(const QString& exe, const QStringList& args, Result callback, int timeout = 12000) {
        if (exe.isEmpty()) { callback({}, QStringLiteral("缺少 USB 组件，请先安装。")); return; }
        ++jobs; emit q->changed();
        auto process = new QProcess(q);
        process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        auto timer = new QTimer(process); timer->setSingleShot(true);
        auto done = std::make_shared<bool>(false);
        auto output = std::make_shared<QByteArray>();
        auto finish = [this, process, timer, done, output, callback](QString error) {
            if (*done) return;
            *done = true; timer->stop();
            output->append(process->readAll());
            if (output->size() > 1024 * 1024) { output->clear(); error = QStringLiteral("USB 组件响应过大。"); }
            if (process->state() != QProcess::NotRunning) process->kill();
            --jobs; process->deleteLater();
            if (!ending) callback(*output, error);
            emit q->changed();
        };
        QObject::connect(process, &QProcess::readyRead, q, [process, output, finish] {
            output->append(process->readAll());
            if (output->size() > 1024 * 1024) finish(QStringLiteral("USB 组件响应过大。"));
        });
        QObject::connect(process, &QProcess::finished, q, [finish, output](int code, QProcess::ExitStatus status) {
            finish(code == 0 && status == QProcess::NormalExit ? QString() :
                QStringLiteral("USB 命令失败 (%1)：%2").arg(code).arg(QString::fromUtf8(*output).left(1800)));
        });
        QObject::connect(process, &QProcess::errorOccurred, q, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(QStringLiteral("USB 组件无法启动。"));
        });
        QObject::connect(timer, &QTimer::timeout, q, [finish] { finish(QStringLiteral("USB 命令超时。")); });
        timer->start(timeout); process->start(exe, args);
    }
    void request(const QString& path, const QJsonObject& body, bool post, Result callback) {
        if (host.serverCert.isNull()) { callback({}, QStringLiteral("请先配对远端主机。")); return; }
        QUrl url; url.setScheme("https"); url.setHost(host.activeAddress.address());
        url.setPort(host.activeHttpsPort); url.setPath(path);
        QNetworkRequest request(url); request.setSslConfiguration(ssl);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(15000);
        auto reply = post ? network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)) : network.get(request);
        ++jobs; emit q->changed();
        QTimer::singleShot(16000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
        reply->setReadBufferSize(1024 * 1024 + 1);
        QObject::connect(reply, &QNetworkReply::readyRead, q, [reply] {
            if (reply->bytesAvailable() > 1024 * 1024) reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, q, [this, reply, callback] {
            --jobs;
            const auto data = reply->readAll();
            QString error;
            if (!PairedCertificate::matches(host.serverCert, reply->sslConfiguration().peerCertificate()))
                error = QStringLiteral("远端证书校验失败或连接不可用。");
            else if (reply->error() != QNetworkReply::NoError)
                error = QStringLiteral("远端 USB：%1 %2").arg(reply->errorString(),
                    QJsonDocument::fromJson(data).object().value("error").toString().left(1500));
            else if (data.size() > 1024 * 1024) error = QStringLiteral("USB 设备响应过大。");
            reply->deleteLater();
            if (!ending) callback(data, error);
            emit q->changed();
        });
    }
    QVariantList devices(bool fromHost) const {
        QVariantList list;
        for (const auto& value : fromHost ? remote : local) {
            const auto obj = value.toObject();
            const auto bus = obj.value("BusId").toString();
            const auto instance = obj.value("InstanceId").toString();
            bool occupied = !obj.value("ClientIPAddress").toString().isEmpty();
            bool shared = !obj.value("PersistedGuid").toString().isEmpty();
            auto c = connections.value(deviceKey(fromHost, bus));
            QString state = !validBus(bus) ? QStringLiteral("不可用 / 已拔出") : occupied ? QStringLiteral("被占用") :
                shared ? QStringLiteral("已共享") : QStringLiteral("未共享");
            if (c) state = c->closing ? QStringLiteral("正在释放") : c->ready ? QStringLiteral("已连接") : QStringLiteral("正在连接");
            list.append(QVariantMap{{"busid", bus}, {"instance", instance},
                {"description", obj.value("Description").toString()}, {"shared", shared},
                {"supported", validBus(bus)}, {"occupied", occupied}, {"active", bool(c)}, {"state", state}});
        }
        return list;
    }
    void detach(const QString& key, const std::shared_ptr<Connection>& c) {
        if (c->hubPort == 0) { connections.remove(key); emit q->changed(); return; }
        const int port = c->hubPort;
        const auto endpoint = QString("-> usbip://127.0.0.1:%1/%2").arg(c->endpointPort).arg(c->bus);
        command(component("USBip/usbip.exe"), {"port", QString::number(port)},
            [this, key, c, port, endpoint](QByteArray data, QString error) {
                bool ours = false;
                for (const auto& line : QString::fromUtf8(data).split('\n'))
                    if (line.trimmed() == endpoint) ours = true;
                if (!error.isEmpty() || !ours) {
                    c->hubPort = 0; connections.remove(key);
                    report(QStringLiteral("USB 通道已关闭；原导入端口已消失或发生变化，未断开其他设备。")); return;
                }
                command(component("USBip/usbip.exe"), {"detach", "--port", QString::number(port)},
                    [this, key, c](QByteArray, QString error) {
                        if (error.isEmpty()) { c->hubPort = 0; connections.remove(key); report(QStringLiteral("设备已释放。")); }
                        else { c->closing = false; report(error); }
                    }, 6000);
            }, 6000);
    }
    void close(const QString& key) {
        auto c = connections.value(key);
        if (!c || c->closing) return;
        c->closing = true;
        if (c->tunnel) { c->tunnel->stop(); c->tunnel->deleteLater(); c->tunnel = nullptr; }
        // Let an in-flight attach finish and report its own port, then detach it.
        // Never detach a guessed port or an unrelated pre-existing USB mapping.
        if (!c->attach) detach(key, c);
        emit q->changed();
    }
    void attach(const QString& key, const std::shared_ptr<Connection>& c, quint16 port) {
        c->endpointPort = port;
        auto exe = component("USBip/usbip.exe");
        if (exe.isEmpty()) { report(QStringLiteral("本机缺少 usbip-win2 导入驱动。")); close(key); return; }
        auto p = new QProcess(q); c->attach = p;
        p->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
        p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        auto timer = new QTimer(p); timer->setSingleShot(true);
        auto done = std::make_shared<bool>(false);
        auto finish = [this, key, c, p, timer, done](bool success) {
            if (*done) return; *done = true; timer->stop();
            auto out = p->readAllStandardOutput().trimmed();
            auto error = QString::fromLocal8Bit(p->readAllStandardError()).left(1800);
            bool numeric = false; int hub = out.toInt(&numeric);
            if (success && numeric && hub > 0 && hub <= 255) c->hubPort = hub;
            else { report(QStringLiteral("USB 导入失败：%1").arg(error)); }
            c->attach = nullptr; p->deleteLater();
            if (!c->hubPort || c->closing) {
                if (!c->closing) close(key); else detach(key, c);
            } else { c->ready = true; report(QStringLiteral("远端 USB 已映射到本机。")); }
        };
        QObject::connect(p, &QProcess::finished, q, [finish](int code, QProcess::ExitStatus status) { finish(code == 0 && status == QProcess::NormalExit); });
        QObject::connect(p, &QProcess::errorOccurred, q, [finish](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) finish(false); });
        QObject::connect(timer, &QTimer::timeout, q, [p] { p->kill(); });
        QObject::connect(p, &QProcess::readyReadStandardOutput, q, [p] { if (p->bytesAvailable() > 65536) p->kill(); });
        QObject::connect(p, &QProcess::readyReadStandardError, q, [p] {
            p->setReadChannel(QProcess::StandardError);
            if (p->bytesAvailable() > 65536) p->kill();
            p->setReadChannel(QProcess::StandardOutput);
        });
        timer->start(14000);
        p->start(exe, {"--tcp-port", QString::number(port), "attach", "--remote", "127.0.0.1", "--bus-id", c->bus, "--once", "--terse"});
    }
};

UsbManager::UsbManager(const NvComputer& host, QObject* parent) : QObject(parent), d(new Impl(this, host)) {}
UsbManager::~UsbManager() {
    d->ending = true;
    // Bound cleanup also covers closing the manager during an attach command.
    const auto ownedConnections = d->connections.values();
    for (auto c : ownedConnections) {
        if (c->tunnel) c->tunnel->stop();
        if (c->attach) {
            c->attach->disconnect(this);
            if (!c->attach->waitForFinished(15000)) { c->attach->kill(); c->attach->waitForFinished(1000); }
            bool ok = false; auto port = c->attach->readAllStandardOutput().trimmed().toInt(&ok);
            if (ok && port > 0 && port <= 255 && c->attach->exitCode() == 0) c->hubPort = port;
        }
        if (c->hubPort) {
            QProcess p;
#ifdef Q_OS_WIN
            p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
            p.start(component("USBip/usbip.exe"), {"port", QString::number(c->hubPort)});
            bool ours = false;
            if (p.waitForFinished(6000) && p.exitCode() == 0) {
                auto endpoint = QString("-> usbip://127.0.0.1:%1/%2").arg(c->endpointPort).arg(c->bus);
                for (const auto& line : QString::fromUtf8(p.readAllStandardOutput()).split('\n'))
                    if (line.trimmed() == endpoint) ours = true;
            } else { p.kill(); p.waitForFinished(1000); }
            if (ours) {
                p.start(component("USBip/usbip.exe"), {"detach", "--port", QString::number(c->hubPort)});
                if (!p.waitForFinished(6000)) { p.kill(); p.waitForFinished(1000); }
            }
        }
    }
    // Child callbacks refer to Impl. Destroy them before Impl is released.
    qDeleteAll(findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly));
    qDeleteAll(findChildren<UsbForwarding::Tunnel*>(QString(), Qt::FindDirectChildrenOnly));
}
QVariantList UsbManager::localDevices() const { return d->devices(false); }
QVariantList UsbManager::remoteDevices() const { return d->devices(true); }
QString UsbManager::status() const { return d->message + (d->localError.isEmpty() ? QString() : QStringLiteral("\n本机：") + d->localError); }
QString UsbManager::hostName() const { return d->host.name; }
bool UsbManager::busy() const { return d->jobs != 0; }

void UsbManager::refresh() {
    if (busy()) return;
    d->report(QStringLiteral("正在检测两端 USB 设备…"));
    d->command(component("usbipd-win/usbipd.exe"), {"state"}, [this](QByteArray data, QString error) {
        d->localError = error;
        auto json = QJsonDocument::fromJson(data).object();
        d->local = json.value("Devices").toArray();
        if (!error.isEmpty()) d->report(QStringLiteral("本机：") + error);
        emit changed();
    });
    d->request("/api/v1/usb-forwarding", {}, false, [this](QByteArray data, QString error) {
        d->token.clear(); d->tunnelPort = 0;
        if (!error.isEmpty()) { d->report(error); return; }
        const auto cap = QJsonDocument::fromJson(data).object();
        int port = cap.value("port").toInt(); auto token = cap.value("token").toString().toLatin1();
        static const QRegularExpression tokenPattern("^[0-9a-fA-F]{64}$");
        if (cap.value("version").toInt() != 1 || !cap.value("available").toBool() ||
            port < 1 || port > 65535 || !tokenPattern.match(QString::fromLatin1(token)).hasMatch()) {
            d->report(QStringLiteral("请在 Sunshine 中开启 USB 转发并重启；同时确认 USB 端口可达。")); return;
        }
        d->token = token; d->tunnelPort = quint16(port); d->bidirectional = cap.value("bidirectional").toBool();
        if (!d->bidirectional) { d->report(QStringLiteral("远端版本仅支持本机 USB → 远端，请升级 Sunshine。")); return; }
        d->request("/api/v1/usb-forwarding/devices", {}, false, [this](QByteArray data, QString error) {
            d->remote = QJsonDocument::fromJson(data).object().value("Devices").toArray();
            if (!error.isEmpty()) d->report(error);
            else d->report(QStringLiteral("双向 USB 已就绪；请选择具体设备共享并连接。"));
            emit changed();
        });
    });
}

void UsbManager::share(bool fromHost, QString bus, QString instance, bool enable) {
    if (busy() || !validBus(bus) || instance.isEmpty() || d->connections.contains(deviceKey(fromHost, bus))) return;
    if (fromHost) {
        d->request("/api/v1/usb-forwarding/devices", {{"operation", enable ? "bind" : "unbind"}, {"busid", bus}, {"instance", instance}}, true,
            [this](QByteArray, QString error) { if (!error.isEmpty()) d->report(error); else refresh(); });
        return;
    }
    d->command(component("usbipd-win/usbipd.exe"), {"state"}, [this, bus, instance, enable](QByteArray data, QString error) {
        bool found = false;
        for (const auto& value : QJsonDocument::fromJson(data).object().value("Devices").toArray()) {
            auto obj = value.toObject();
            if (obj.value("BusId").toString() == bus && obj.value("InstanceId").toString() == instance && obj.value("ClientIPAddress").toString().isEmpty()) found = true;
        }
        if (!error.isEmpty() || !found) { d->report(QStringLiteral("设备状态已改变或正被占用，请刷新后重试。") + error); return; }
#ifdef Q_OS_WIN
        auto exe = component("usbipd-win/usbipd.exe");
        auto args = (enable ? QStringLiteral("bind --busid ") : QStringLiteral("unbind --busid ")) + bus;
        SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; info.lpFile = reinterpret_cast<LPCWSTR>(exe.utf16());
        info.lpParameters = reinterpret_cast<LPCWSTR>(args.utf16()); info.nShow = SW_HIDE;
        if (!ShellExecuteExW(&info) || !info.hProcess) { d->report(QStringLiteral("未完成管理员授权，设备共享未改变。")); return; }
        ++d->jobs; emit changed();
        auto timer = new QTimer(this); timer->setInterval(100);
        auto handle = std::shared_ptr<void>(info.hProcess, [](void* h) { CloseHandle(h); });
        connect(timer, &QTimer::timeout, this, [this, timer, handle] {
            DWORD code = STILL_ACTIVE;
            if (!GetExitCodeProcess(handle.get(), &code) || code == STILL_ACTIVE) return;
            timer->stop(); timer->deleteLater(); --d->jobs;
            if (code) d->report(QStringLiteral("共享操作失败 (%1)，请检查 usbipd 服务。预设不强制替换驱动。").arg(code));
            else refresh();
            emit changed();
        });
        timer->start();
#endif
    });
}

void UsbManager::connectDevice(bool fromHost, QString bus, QString instance) {
    const auto key = deviceKey(fromHost, bus);
    if (busy() || !validBus(bus) || d->connections.contains(key)) return;
    if (d->token.isEmpty() || !d->tunnelPort || (fromHost && !d->bidirectional)) { d->report(QStringLiteral("请刷新并确认 Sunshine USB 转发已启用。")); return; }
    auto connectSelected = [this, fromHost, bus, instance, key](QByteArray data, QString error) {
        if (!error.isEmpty()) { d->report(error); return; }
        auto devices = QJsonDocument::fromJson(data).object().value("Devices").toArray();
        if (fromHost) d->remote = devices; else d->local = devices;
    bool found = false;
    for (const auto& value : fromHost ? d->remote : d->local) {
        auto obj = value.toObject();
        if (obj.value("BusId").toString() == bus && obj.value("InstanceId").toString() == instance &&
            !obj.value("PersistedGuid").toString().isEmpty() && obj.value("ClientIPAddress").toString().isEmpty()) found = true;
    }
    if (!found) { d->report(QStringLiteral("设备已变化，请先刷新并共享该设备。")); return; }
    UsbForwarding::TunnelConfig config;
    config.host = d->host.activeAddress.address(); config.port = d->tunnelPort;
    config.sessionToken = d->token; config.busId = bus.toUtf8(); config.importFromHost = fromHost;
    config.sslConfiguration = d->ssl; config.pinnedServerCertificate = d->host.serverCert;
    auto c = std::make_shared<Impl::Connection>(); c->bus = bus; c->fromHost = fromHost;
    c->tunnel = new UsbForwarding::Tunnel(config, this); d->connections.insert(key, c);
    connect(c->tunnel, &UsbForwarding::Tunnel::listening, this, [this, key, c](quint16 port) { if (!c->closing) d->attach(key, c, port); });
    connect(c->tunnel, &UsbForwarding::Tunnel::forwarding, this, [this, c] {
        if (!c->fromHost) { c->ready = true; d->report(QStringLiteral("本机 USB 通道已建立，等待远端驱动完成枚举。")); }
        emit changed();
    });
    connect(c->tunnel, &UsbForwarding::Tunnel::finished, this, [this, key](QString error) {
        d->report(error.isEmpty() ? QStringLiteral("USB 连接已断开。") : error); d->close(key);
    });
    QString startError;
    if (!c->tunnel->start(&startError)) { d->report(startError); d->close(key); }
    emit changed();
    };
    if (fromHost) d->request("/api/v1/usb-forwarding/devices", {}, false, connectSelected);
    else d->command(component("usbipd-win/usbipd.exe"), {"state"}, connectSelected);
}
void UsbManager::disconnectDevice(bool fromHost, QString bus) { d->close(deviceKey(fromHost, bus)); }
void UsbManager::disconnectAll() { for (const auto& key : d->connections.keys()) d->close(key); }
void UsbManager::installComponents() {
#ifdef Q_OS_WIN
    auto script = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/USB/Install-USB.ps1");
    if (!QFileInfo::exists(script)) { d->report(QStringLiteral("缺少组件安装脚本，请使用完整安装包。")); return; }
    auto executable = QDir::toNativeSeparators(qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe");
    auto args = QStringLiteral("-NoProfile -ExecutionPolicy Bypass -File \"%1\"").arg(script);
    auto result = ShellExecuteW(nullptr, L"runas", reinterpret_cast<LPCWSTR>(executable.utf16()), reinterpret_cast<LPCWSTR>(args.utf16()), nullptr, SW_SHOWNORMAL);
    d->report(reinterpret_cast<INT_PTR>(result) > 32 ? QStringLiteral("请完成组件安装，必要时重启，再刷新设备。") : QStringLiteral("未启动组件安装。"));
#endif
}
