// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once
#include <QThread>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QUrl>
#include <atomic>
class NvComputer;
struct SDL_Window;
class FocusIme : public QThread {
public:
    explicit FocusIme(NvComputer* computer);
    ~FocusIme() override;
    void setStreamWindow(SDL_Window* window);
protected:
    void run() override;
private:
    QUrl base;
    QSslCertificate certificate;
    QSslConfiguration ssl;
    std::atomic<bool> stopping{false};
    void* streamWindow = nullptr;
    void* initialWindow = nullptr;
};
