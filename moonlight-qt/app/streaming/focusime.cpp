// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#include "focusime.h"
#include "backend/nvcomputer.h"
#include "backend/identitymanager.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#ifdef Q_OS_WIN
#include <SDL_syswm.h>
#include "focusime_win.h"
#endif

FocusIme::FocusIme(NvComputer* computer) :
    certificate(computer->serverCert), ssl(IdentityManager::get()->getSslConfig())
{
    base.setScheme("https"); base.setHost(computer->activeAddress.address()); base.setPort(computer->activeHttpsPort);
    base.setPath(QStringLiteral("/api/v2/focus-ime/state"));
#ifdef Q_OS_WIN
    initialWindow = GetForegroundWindow();
#endif
}
FocusIme::~FocusIme() { stopping = true; wait(); }
void FocusIme::setStreamWindow(SDL_Window* window) {
#ifdef Q_OS_WIN
    SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) streamWindow = info.info.win.window;
#else
    Q_UNUSED(window);
#endif
}
void FocusIme::run() {
#ifdef Q_OS_WIN
    if (certificate.isNull()) { qWarning() << "IME sync requires a paired certificate"; return; }
    QNetworkAccessManager network;
    QObject::connect(&network, &QNetworkAccessManager::encrypted, &network, [&](QNetworkReply* reply) {
        if (reply->sslConfiguration().peerCertificate() != certificate) reply->abort();
    });
    QObject::connect(&network, &QNetworkAccessManager::sslErrors, &network,
                     [&](QNetworkReply* reply, const QList<QSslError>& errors) {
        for (const auto& error : errors) if (error.certificate() != certificate) return;
        reply->ignoreSslErrors(errors);
    });
    auto local = focusime::inspect(static_cast<HWND>(initialWindow));
    focusime::Mode lastReported = focusime::Mode::unknown;
    unsigned failedApplies = 0;
    unsigned failedHostApplies = 0;
    bool legacy = false;
    QString lastError;
    qInfo() << "Focus-directed IME synchronization started (ImeSync2)";
    while (!stopping) {
        const bool streamAtRequest = GetForegroundWindow() == static_cast<HWND>(streamWindow);
        auto active = focusime::foreground();
        if (!streamAtRequest && active.root && active.root != static_cast<HWND>(streamWindow)) local = active;
        // Local focus never receives a remote write. The actual local state is
        // sent directly, so disabling Shift locally remains meaningful.
        const auto desired = focusime::requestedMode(streamAtRequest, active.mode);
        auto wire = focusime::encode(desired, '2');
        QNetworkRequest request(base);
        request.setSslConfiguration(ssl);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        auto reply = network.post(request, legacy ? QByteArray() : QByteArray(wire.data(), int(wire.size())));
        reply->setReadBufferSize(6);
        QEventLoop loop; QTimer timer; timer.setInterval(25);
        unsigned ticks = 0;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            if (reply->bytesAvailable() > 5) reply->abort();
        });
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
            if (stopping || ++ticks >= 80) reply->abort();
        });
        timer.start(); loop.exec(); timer.stop();
        auto bytes = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto peer = reply->sslConfiguration().peerCertificate();
        bool trusted = peer == certificate;
        bool mismatched = !peer.isNull() && !trusted;
        bool ok = trusted && status == 200 && reply->error() == QNetworkReply::NoError;
        delete reply;
        if (stopping) break;
        focusime::Mode remote = focusime::Mode::unknown;
        if (status == 404 && !legacy && trusted) {
            // Old Sunshine remains usable. Never restore the old behaviour that
            // repeatedly overwrote local input while the user was working there.
            legacy = true;
            base.setPath(QStringLiteral("/api/v1/focus-ime/state"));
            qWarning() << "Sunshine requires ImeSync2 for local-to-host IME sync; local changes are preserved";
            continue;
        }
        if (ok && focusime::decode(std::string(bytes.constData(), size_t(bytes.size())), remote, legacy ? '1' : '2')) {
            lastError.clear();
            if (!legacy && focusime::known(desired) && remote != desired) {
                if (++failedHostApplies == 40) qWarning() << "IME sync: remote input context did not accept local mode";
            } else failedHostApplies = 0;
            if (remote != focusime::Mode::unknown) {
                const bool streamAtReply = GetForegroundWindow() == static_cast<HWND>(streamWindow);
                bool applied = true;
                if (focusime::mayApplyRemote(streamAtRequest, streamAtReply, remote)) {
                    applied = focusime::apply(local, remote, static_cast<HWND>(streamWindow));
                }
                if (remote != lastReported) {
                    qInfo() << "Focus-directed IME host state:" << (remote == focusime::Mode::chinese ? "Chinese" : "English");
                    lastReported = remote;
                }
                if (applied) failedApplies = 0;
                else if (++failedApplies == 40) {
                    qWarning() << "IME sync waiting for an accessible Microsoft Pinyin input context";
                }
            }
        } else {
            QString error = mismatched ? "Host certificate mismatch" : QString("Host IME request failed (%1)").arg(status);
            if (error != lastError) { lastError = error; qWarning() << error; }
            // Never keep applying a stale remote state during an outage.
            if (status == 404 || mismatched) break;
        }
        for (unsigned i = 0; i < 10 && !stopping; ++i) QThread::msleep(25);
    }
#endif
}
