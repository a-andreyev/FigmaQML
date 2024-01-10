import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

Dialog {
    id: dialog
    title: "Qt For MCU Settings"

    background: Rectangle {
            color: "lightgray"
    }
    property color textBg: "white"
    property color textBgBorder: "gray"

    property alias saveAsApp : saveAsApp.checked

    signal saveRequest;

    readonly property var params: {
        'qtDir': qtDir.text,
        'qulVer': qulVer.text,
        'qulPlatform': qulPlatform.text,
        'qtLicense': qtLicense.text,
        'platformTools': platformTools.text
    }

    Settings {
           id: settings
           property alias qtDirValue: qtDir.text
           property alias qulVerValue: qulVer.text
           property alias qulPlatformValue: qulPlatform.text
           property alias qtLicenseValue: qtLicense.text
           property alias platformToolsValue: platformTools.text
       }

    component Input : RowLayout {
        id: row
        property string text
        property string buttonText
        signal clicked
        Layout.minimumWidth: rect.width + 10 + button.width + spacing
        Rectangle {
            id: rect
            color: dialog.textBg
            border.color: dialog.textBgBorder
            Layout.preferredWidth: input.width + 10
            Layout.preferredHeight: button.height - border.width * 2
            TextInput {
                id: input
                anchors.centerIn: parent
                text: row.text
                width: metrics.width
            }

            TextMetrics {
                id:     metrics
                font:   input.font
                text:   input.text
            }
        }
        Button {
            id: button
            Layout.alignment: Qt.AlignRight
            visible: row.buttonText.length > 0
            text: row.buttonText
            onClicked: row.clicked()
        }
    }

    ColumnLayout {

        Text {text: "Qt DIR";font.weight: Font.Medium}
        Input {
            id: qtDir
            text: "/opt/Qt"
            buttonText: "Select..."
            Layout.preferredWidth: parent.width
            onClicked: {
                folderDialog.title = "Select a Qt Dir"
                folderDialog.target = this
                folderDialog.open()
            }
        }

        Text {
            text: "Qul Version"
        }
        Input {
            id: qulVer
            text: "2.6.0"
        }

        Text {text: "Qul Platform";font.weight: Font.Medium}
        Input {
            id: qulPlatform
            text: "STM32F769I-DISCOVERY-baremetal"
        }

        Text {text: "Qt License";font.weight: Font.Medium}
        Input {
            id: qtLicense
            text: "./qt-license.txt"
            buttonText: "Select..."
            Layout.preferredWidth: parent.width
            onClicked: {
                fileDialog.title = "Select a Qt License file"
                fileDialog.target = this
                fileDialog.open()
            }
        }

        Text {text: "Platform tools";font.weight: Font.Medium}
        Input {
            id: platformTools
            text: "         "
            buttonText: "Select..."
            Layout.preferredWidth: parent.width
            onClicked: {
                folderDialog.title = "Select platform tools folder"
                folderDialog.target = this
                folderDialog.open()
            }
        }
    }

    footer: Row {
        DialogButtonBox {
            Button {
                text: qsTr("Execute...")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }

            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }

            Button {
                text: qsTr("Save...")
                onClicked: dialog.saveRequest();
            }


            onAccepted: dialog.done(Dialog.Accepted)
            onRejected: dialog.done(Dialog.Rejected)

            }
        CheckBox {
            id: saveAsApp
            text: "Save as app"
        }
    }

    FileDialog {
        id: fileDialog
        property var target
        onAccepted: {
            target.text = selectedFile.toString().substring(7)
        }
    }

    FolderDialog {
        id: folderDialog
        property var target
        onAccepted: {
            target.text = selectedFolder.toString().substring(7)
        }
    }
}