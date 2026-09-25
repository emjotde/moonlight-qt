import QtQuick 2.0
import QtQuick.Controls 2.2

Item {
    Component.onCompleted: errorDialog.open()

    ErrorMessageDialog {
        id: errorDialog
        text: uriLaunchError
        onClosed: Qt.quit()
    }
}
