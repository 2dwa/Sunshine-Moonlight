// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#pragma once
#include <QObject>
#include <QVariantList>
#include <memory>
class NvComputer;
class UsbManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList localDevices READ localDevices NOTIFY changed)
    Q_PROPERTY(QVariantList remoteDevices READ remoteDevices NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit UsbManager(const NvComputer& host, QObject* parent = nullptr);
    ~UsbManager() override;
    QVariantList localDevices() const;
    QVariantList remoteDevices() const;
    QString status() const;
    QString hostName() const;
    bool busy() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void share(bool fromHost, QString busid, QString instance, bool enable);
    Q_INVOKABLE void connectDevice(bool fromHost, QString busid, QString instance);
    Q_INVOKABLE void disconnectDevice(bool fromHost, QString busid);
    Q_INVOKABLE void disconnectAll();
    Q_INVOKABLE void installComponents();
signals:
    void changed();
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
