import QtQuick 2.0
import QtQuick.Controls 2.2

NavigableMessageDialog {
    objectName: "pairingDialog"
    property string pin: "0000"

    modal: true
    closePolicy: Popup.CloseOnEscape
    text: qsTr("Please enter %1 on your host PC. This dialog will close when pairing is completed.").arg(pin) + "\n\n" + qsTr("If your host PC is running Sunshine, navigate to the Sunshine web UI to enter the PIN.")
    standardButtons: Dialog.Cancel
}
