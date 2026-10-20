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
    signal powerScheduled(string action)
    property string action: "exit"
    property bool submitting: false
    property var powerRequest: null
    readonly property string actionLabel: action === "restart" ? qsTr("Restart") :
                                          action === "shutdown" ? qsTr("Shut down") : qsTr("Exit experience")

    function request() { requestAction("exit") }

    function requestAction(requestedAction) {
        action = requestedAction || "exit"
        if (action !== "exit" && !KioskBridge.operatorPowerAvailable()) return
        if (action === "exit" && !KioskBridge.operatorPinRequired()) {
            authorized()
            return
        }
        errorLabel.text = ""
        pin.clear()
        visible = true
        pin.forceActiveFocus()
    }

    function dismiss() {
        if (powerRequest) {
            powerRequest.onreadystatechange = function() {}
            powerRequest.abort()
            powerRequest = null
        }
        submitting = false
        requestTimeout.stop()
        pin.clear()
        errorLabel.text = ""
        visible = false
    }

    function cancel() {
        // Once submitted, aborting HTTP does not cancel a Windows power action.
        if (submitting) return
        dismiss()
        cancelled()
    }

    function submit() {
        if (retryTimer.running || submitting || pin.text.length < 4) return
        if (action !== "exit") {
            submitPower()
            return
        }
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

    function submitPower() {
        submitting = true
        errorLabel.text = ""
        var xhr = new XMLHttpRequest()
        powerRequest = xhr
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE || powerRequest !== xhr) return
            requestTimeout.stop()
            powerRequest = null
            submitting = false
            if (xhr.status === 202) {
                visible = false
                powerScheduled(action)
            } else {
                var message = qsTr("Could not contact HydraHead. Please try again.")
                try { message = JSON.parse(xhr.responseText).error || message } catch(e) {}
                errorLabel.text = message
                retryTimer.restart()
                pin.forceActiveFocus()
            }
        }
        xhr.open("POST", "http://127.0.0.1:9740/api/v1/operator/power")
        xhr.setRequestHeader("Content-Type", "application/json")
        var body = JSON.stringify({action: action, pin: pin.text})
        pin.clear()
        requestTimeout.restart()
        xhr.send(body)
    }

    function focusInput() { pin.forceActiveFocus() }
    Timer { id: retryTimer; interval: 2100 }
    Timer {
        id: requestTimeout
        interval: 8000
        onTriggered: {
            var xhr = powerRequest
            powerRequest = null
            if (xhr) xhr.abort()
            submitting = false
            errorLabel.text = qsTr("No response from HydraHead. The power action may already be scheduled.")
            retryTimer.restart()
        }
    }
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
            text: action === "exit" ? qsTr("Enter the PIN to exit this experience.") :
                  action === "restart" ? qsTr("Enter the PIN to restart this machine. All experiences will close in 10 seconds. Unsaved progress will be lost.") :
                  qsTr("Enter the PIN to shut down this machine. All experiences will close in 10 seconds. Unsaved progress will be lost. Turning it on again requires the power button or remote power control.")
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
            enabled: !prompt.submitting
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
                enabled: !prompt.submitting
                onClicked: prompt.cancel()
            }
            Button {
                objectName: "operatorPinExit"
                text: prompt.submitting ? qsTr("Please wait…") : prompt.actionLabel
                enabled: pin.text.length >= 4 && !retryTimer.running && !prompt.submitting
                onClicked: prompt.submit()
            }
        }
    }
}
