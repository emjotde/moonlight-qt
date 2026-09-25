import QtQuick 2.0
import QtQuick.Controls 2.2

import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0

Item {
    property var requestLauncher: null
    readonly property var activeLauncher: requestLauncher ? requestLauncher : (typeof launcher !== "undefined" ? launcher : null)
    property bool sessionStarted: false

    function finishExternalLaunch() {
        if (activeLauncher && activeLauncher.isExternalRequest()) {
            stackView.pop();
        } else {
            Qt.quit();
        }
    }

    function onSearchingComputer() {
        stageLabel.text = qsTr("Establishing connection to PC...");
    }

    function onSearchingApp() {
        stageLabel.text = qsTr("Loading app list...");
    }

    function onSessionCreated(appName, session) {
        sessionStarted = true;
        var component = Qt.createComponent("StreamSegue.qml");
        var segue = component.createObject(stackView, {
            "appName": appName,
            "session": session,
            "quitAfter": !activeLauncher.isExternalRequest()
        });
        stackView.push(segue);
    }

    function onLaunchFailed(message) {
        errorDialog.text = message;
        errorDialog.open();
        console.error(message);
    }

    function onAppQuitRequired(appName) {
        quitAppDialog.appName = appName;
        quitAppDialog.open();
    }

    function onPairingRequired(hostName, pin) {
        stageLabel.text = qsTr("Pairing with %1...").arg(hostName);
        pairingDialog.pin = pin;
        pairingDialog.open();
    }

    function onPairingFinished() {
        pairingDialog.close();
    }

    function onExternalLaunchConfirmationRequired(details) {
        externalLaunchDialog.details = details;
        externalLaunchDialog.open();
    }

    function onExternalLaunchCancelled() {
        finishExternalLaunch();
    }

    StackView.onActivated: {
        if (sessionStarted && activeLauncher.isExternalRequest()) {
            stackView.pop();
            return;
        }
        if (!activeLauncher.isExecuted()) {
            toolBar.visible = false;

            // Normally this is enabled by PcView, but we will won't
            // load PcView when streaming from the command-line.
            SdlGamepadKeyNavigation.enable();

            activeLauncher.searchingComputer.connect(onSearchingComputer);
            activeLauncher.searchingApp.connect(onSearchingApp);
            activeLauncher.sessionCreated.connect(onSessionCreated);
            activeLauncher.failed.connect(onLaunchFailed);
            activeLauncher.appQuitRequired.connect(onAppQuitRequired);
            activeLauncher.pairingRequired.connect(onPairingRequired);
            activeLauncher.pairingFinished.connect(onPairingFinished);
            activeLauncher.externalLaunchConfirmationRequired.connect(onExternalLaunchConfirmationRequired);
            activeLauncher.externalLaunchCancelled.connect(onExternalLaunchCancelled);
            activeLauncher.execute(ComputerManager);
        }
    }

    Component.onDestruction: {
        if (requestLauncher) {
            uriLaunchManager.releaseLauncher(requestLauncher);
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 5

        BusyIndicator {
            id: stageSpinner
        }

        Label {
            id: stageLabel
            height: stageSpinner.height
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    ErrorMessageDialog {
        id: errorDialog
        onClosed: finishExternalLaunch()
    }

    PairingDialog {
        id: pairingDialog
        onRejected: finishExternalLaunch()
    }

    ExternalLaunchDialog {
        id: externalLaunchDialog
        onLaunchOnce: activeLauncher.approveExternalLaunch(false)
        onAlwaysAllow: activeLauncher.approveExternalLaunch(true)
        onCancelled: activeLauncher.cancelExternalLaunch()
    }

    NavigableMessageDialog {
        id: quitAppDialog
        property string appName: ""

        text: qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml");
            var params = {
                "appName": appName,
                "quitRunningAppFn": function () {
                    activeLauncher.quitRunningApp();
                }
            };
            stackView.push(component.createObject(stackView, params));
        }

        onAccepted: quitApp()
        onRejected: finishExternalLaunch()
    }
}
