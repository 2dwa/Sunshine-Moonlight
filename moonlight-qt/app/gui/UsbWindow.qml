// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

ApplicationWindow {
    id: usbWindow
    visible: true
    width: 860; height: 660
    minimumWidth: 700; minimumHeight: 480
    title: "双向 USB — " + usbManager.hostName
    Component.onCompleted: usbManager.refresh()
    ColumnLayout {
        objectName: "usbContent"
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        Label { text: "USB 设备映射"; font.pixelSize: 23 }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "先共享设备，再选择连接。连接期间设备由接收方独占；释放前请结束文件写入。关闭此窗口会释放本窗口的全部连接。"
        }
        RowLayout {
            Button { text: "刷新设备"; enabled: !usbManager.busy; onClicked: usbManager.refresh() }
            Button { text: "安装本机 USB 组件"; enabled: !usbManager.busy; onClicked: usbManager.installComponents() }
            Button { text: "全部释放"; onClicked: usbManager.disconnectAll() }
            BusyIndicator { running: usbManager.busy; implicitWidth: 32; implicitHeight: 32 }
        }
        TabBar {
            id: directions; Layout.fillWidth: true
            TabButton { text: "本机 USB → 远端" }
            TabButton { text: "远端 USB → 本机" }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: directions.currentIndex === 0 ? "选择连接在本机的 USB 设备，交给远端使用。" : "选择连接在远端的 USB 设备，映射到本机使用。"
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
            model: directions.currentIndex === 0 ? usbManager.localDevices : usbManager.remoteDevices
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                width: ListView.view.width; height: 100; radius: 6; color: "#292b35"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: modelData.description; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: "USB " + modelData.busid + " · " + modelData.state; color: "#bfc5d3" }
                    }
                    Button {
                        text: modelData.shared ? "取消共享" : "共享"
                        enabled: !usbManager.busy && modelData.supported && !modelData.active && !modelData.occupied
                        onClicked: usbManager.share(directions.currentIndex === 1, modelData.busid, modelData.instance, !modelData.shared)
                    }
                    Button {
                        text: modelData.active ? "释放" : "连接"
                        enabled: modelData.active || (!usbManager.busy && modelData.supported && modelData.shared && !modelData.occupied)
                        onClicked: {
                            if (modelData.active) usbManager.disconnectDevice(directions.currentIndex === 1, modelData.busid)
                            else usbManager.connectDevice(directions.currentIndex === 1, modelData.busid, modelData.instance)
                        }
                    }
                }
            }
        }
        Label { text: usbManager.status; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#dfc98d" }
        Label {
            text: "Sunshine 需开启 USB 转发并重启。两端首次使用需安装 USB 组件；摄像头、声卡等设备的兼容性取决于设备与驱动。共享设置会保留，取消共享可撤销。"
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#a9b0bd"
        }
    }
}
