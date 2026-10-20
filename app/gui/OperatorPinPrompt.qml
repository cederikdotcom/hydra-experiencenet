import QtQuick 2.14
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import KioskBridge 1.0

Rectangle {
    id: prompt
    visible: false
    color: "#f20a0a0a"
    signal authorized()
    signal cancelled()

    function request() {
        if (!KioskBridge.operatorPinRequired()) {
            authorized()
            return
        }
        errorLabel.text = ""
        pin.clear()
        visible = true
        pin.forceActiveFocus()
    }

    function dismiss() {
        pin.clear()
        errorLabel.text = ""
        visible = false
    }

    function cancel() {
        dismiss()
        cancelled()
    }

    function submit() {
        if (retryTimer.running || pin.text.length < 4) return
        var accepted = KioskBridge.verifyOperatorPin(pin.text)
        pin.clear()
        if (accepted) {
            visible = false
            authorized()
        } else {
            errorLabel.text = qsTr("Incorrect PIN. Try again in a moment.")
            retryTimer.restart()
            pin.forceActiveFocus()
        }
    }

    function focusInput() { pin.forceActiveFocus() }
    Timer { id: retryTimer; interval: 2100 }
    MouseArea { anchors.fill: parent }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(360, parent.width - 40)
        spacing: 16
        Label {
            text: qsTr("Operator PIN")
            color: "white"
            font.pixelSize: 26
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Enter the PIN to exit this experience.")
            color: "#d4d4d8"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        TextField {
            id: pin
            objectName: "operatorPinInput"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            passwordMaskDelay: 0
            maximumLength: 12
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,12}/ }
            selectByMouse: false
            placeholderText: qsTr("PIN")
            onAccepted: prompt.submit()
            Keys.onEscapePressed: prompt.cancel()
        }
        Label {
            id: errorLabel
            Layout.fillWidth: true
            color: "#fca5a5"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 16
            Button {
                objectName: "operatorPinCancel"
                text: qsTr("Cancel")
                onClicked: prompt.cancel()
            }
            Button {
                objectName: "operatorPinExit"
                text: qsTr("Exit experience")
                enabled: pin.text.length >= 4 && !retryTimer.running
                onClicked: prompt.submit()
            }
        }
    }
}
