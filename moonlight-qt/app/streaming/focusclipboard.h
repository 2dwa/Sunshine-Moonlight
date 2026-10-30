// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once
#include <QThread>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QUrl>
#include <atomic>
class NvComputer;
class FocusClipboard : public QThread {
public:
    explicit FocusClipboard(NvComputer* computer);
    ~FocusClipboard() override;
protected:
    void run() override;
private:
    QUrl base;
    QSslCertificate certificate;
    QSslConfiguration ssl;
    std::atomic<bool> stopping{false};
};
