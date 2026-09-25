import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

NavigableDialog {
    id: dialog

    property var appModel
    property int appId
    property string appName
    property string errorText

    title: qsTr("Stream settings: %1").arg(appName)
    width: Math.min(540, parent.width - 40)
    height: Math.min(760, parent.height - 40)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    function openForApp(id, name) {
        appId = id;
        appName = name;
        var profile = appModel.getAppStreamingSettings(appId);
        useGlobal.checked = !profile.enabled;
        widthField.text = profile.width.toString();
        heightField.text = profile.height.toString();
        fpsField.text = profile.fps.toString();
        customBitrate.checked = profile.bitrateKbps !== 0;
        bitrateField.text = (customBitrate.checked ? profile.bitrateKbps : profile.globalBitrateKbps).toString();
        selectValue(windowMode, profile.windowMode);
        selectValue(keyboardMode, profile.captureSysKeysMode);
        displayModel.clear();
        displayModel.append({
            "text": qsTr("Monitor containing Moonlight"),
            "id": ""
        });
        var displays = appModel.getStreamDisplays();
        for (var i = 0; i < displays.length; ++i) {
            displayModel.append(displays[i]);
        }
        preferredDisplay.currentIndex = 0;
        if (profile.preferredDisplay) {
            var found = false;
            for (var j = 1; j < displayModel.count; ++j) {
                if (displayModel.get(j).id === profile.preferredDisplay) {
                    preferredDisplay.currentIndex = j;
                    found = true;
                    break;
                }
            }
            if (!found) {
                displayModel.append({
                    "text": qsTr("Saved monitor is currently unavailable"),
                    "id": profile.preferredDisplay
                });
                preferredDisplay.currentIndex = displayModel.count - 1;
            }
        }
        errorText = profile.error;
        open();
    }

    function selectValue(combo, value) {
        combo.currentIndex = 0;
        for (var i = 0; i < combo.model.length; ++i) {
            if (combo.model[i].value === value) {
                combo.currentIndex = i;
                break;
            }
        }
    }

    function validNumber(field) {
        return field.acceptableInput && /^[0-9]+$/.test(field.text);
    }

    function isInputValid() {
        return useGlobal.checked || (validNumber(widthField) && validNumber(heightField) && validNumber(fpsField) && (!customBitrate.checked || validNumber(bitrateField)));
    }

    function saveProfile() {
        if (!isInputValid()) {
            errorText = qsTr("Enter valid values in all enabled fields.");
            return;
        }

        errorText = useGlobal.checked ? appModel.removeAppStreamingSettings(appId) : appModel.saveAppStreamingSettings(appId, Number(widthField.text), Number(heightField.text), Number(fpsField.text), customBitrate.checked ? Number(bitrateField.text) : 0, windowMode.model[windowMode.currentIndex].value, keyboardMode.model[keyboardMode.currentIndex].value, displayModel.get(preferredDisplay.currentIndex).id);
        if (!errorText) {
            accept();
        }
    }

    function resetProfile() {
        errorText = appModel.removeAppStreamingSettings(appId);
        if (!errorText) {
            accept();
        }
    }

    onOpened: useGlobal.forceActiveFocus()

    contentItem: ScrollView {
        id: fieldsScroll
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: fieldsScroll.availableWidth
            spacing: 12

            CheckBox {
                id: useGlobal
                objectName: "useGlobal"
                text: qsTr("Use global settings")
                checked: true
            }

            GridLayout {
                columns: 2
                enabled: !useGlobal.checked
                Layout.fillWidth: true

                Label {
                    text: qsTr("Width (256-8192):")
                }
                TextField {
                    id: widthField
                    objectName: "widthField"
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 4
                    validator: IntValidator {
                        bottom: 256
                        top: 8192
                        locale: "C"
                    }
                    onAccepted: dialog.saveProfile()
                    Accessible.name: qsTr("Width")
                }

                Label {
                    text: qsTr("Height (256-8192):")
                }
                TextField {
                    id: heightField
                    objectName: "heightField"
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 4
                    validator: IntValidator {
                        bottom: 256
                        top: 8192
                        locale: "C"
                    }
                    onAccepted: dialog.saveProfile()
                    Accessible.name: qsTr("Height")
                }

                Label {
                    text: qsTr("FPS (10-9999):")
                }
                TextField {
                    id: fpsField
                    objectName: "fpsField"
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 4
                    validator: IntValidator {
                        bottom: 10
                        top: 9999
                        locale: "C"
                    }
                    onAccepted: dialog.saveProfile()
                    Accessible.name: qsTr("FPS")
                }

                CheckBox {
                    id: customBitrate
                    objectName: "customBitrate"
                    text: qsTr("Use custom bitrate")
                    Layout.columnSpan: 2
                }

                Label {
                    text: qsTr("Bitrate (Kbps):")
                    enabled: customBitrate.checked
                }
                TextField {
                    id: bitrateField
                    objectName: "bitrateField"
                    enabled: customBitrate.checked
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 6
                    validator: IntValidator {
                        bottom: 500
                        top: 500000
                        locale: "C"
                    }
                    onAccepted: dialog.saveProfile()
                    Accessible.name: qsTr("Bitrate in Kbps")
                }

                Label {
                    text: qsTr("Launch mode:")
                }
                ComboBox {
                    id: windowMode
                    objectName: "windowMode"
                    Layout.fillWidth: true
                    textRole: "text"
                    model: [
                        {
                            "text": qsTr("Use global setting"),
                            "value": -1
                        },
                        {
                            "text": qsTr("Windowed"),
                            "value": 2
                        },
                        {
                            "text": qsTr("Borderless fullscreen"),
                            "value": 1
                        },
                        {
                            "text": qsTr("Exclusive fullscreen"),
                            "value": 0
                        }
                    ]
                    Accessible.name: qsTr("Launch mode")
                }

                Label {
                    text: qsTr("Preferred monitor:")
                }
                ComboBox {
                    id: preferredDisplay
                    objectName: "preferredDisplay"
                    Layout.fillWidth: true
                    textRole: "text"
                    model: ListModel {
                        id: displayModel
                    }
                    Accessible.name: qsTr("Preferred monitor")
                }

                Label {
                    text: qsTr("System shortcuts:")
                }
                ComboBox {
                    id: keyboardMode
                    objectName: "keyboardMode"
                    Layout.fillWidth: true
                    textRole: "text"
                    model: [
                        {
                            "text": qsTr("Use global setting"),
                            "value": -1
                        },
                        {
                            "text": qsTr("Auto (remote only in fullscreen)"),
                            "value": 1
                        },
                        {
                            "text": qsTr("Local"),
                            "value": 0
                        },
                        {
                            "text": qsTr("Remote"),
                            "value": 2
                        }
                    ]
                    Accessible.name: qsTr("System shortcuts")
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Without a custom bitrate, the global bitrate is used. Codec, audio and other settings remain global.")
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Auto keeps system shortcuts local while windowed and sends them to the host in fullscreen. Normal typing still goes to the host. Dock changes apply only to the current session.")
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Changes apply the next time you launch or resume this app. Your host must support the selected resolution and orientation.")
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: !!dialog.errorText
                text: dialog.errorText
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "resetButton"
            text: qsTr("Reset to global")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.resetProfile()
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "saveButton"
            text: qsTr("Save")
            enabled: dialog.isInputValid()
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.saveProfile()
        }
    }
}
