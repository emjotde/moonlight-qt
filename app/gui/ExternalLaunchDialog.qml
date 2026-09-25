import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

NavigableDialog {
    id: dialog
    objectName: "externalLaunchDialog"

    property string details

    signal launchOnce
    signal alwaysAllow
    signal cancelled

    title: qsTr("External Moonlight launch request")
    modal: true
    closePolicy: Popup.CloseOnEscape

    onRejected: cancelled()

    ColumnLayout {
        spacing: 12

        Label {
            Layout.maximumWidth: 500
            text: qsTr("A website or shortcut requested a Moonlight stream. Review the destination before continuing.")
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "externalLaunchDetails"
            Layout.maximumWidth: 500
            text: dialog.details
            wrapMode: Text.Wrap
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "launchOnceButton"
            text: qsTr("Launch once")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                dialog.close();
                dialog.launchOnce();
            }
        }
        Button {
            objectName: "alwaysAllowButton"
            text: qsTr("Always allow links for this host")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                dialog.close();
                dialog.alwaysAllow();
            }
        }
        Button {
            objectName: "cancelExternalLaunchButton"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
