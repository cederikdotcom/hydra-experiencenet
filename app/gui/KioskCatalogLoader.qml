import QtQuick 2.9

// The head may be unavailable during boot or an update. Retry the entire
// config/catalog sequence, including connection failures and hung requests.
Item {
    id: loader
    visible: false
    property string baseUrl: "http://127.0.0.1:9740"
    property var currentRequest: null
    signal loadStarted()
    signal loaded(var experiences, string district, string venue)
    signal unavailable()

    function cancelRequest() {
        watchdog.stop()
        var old = currentRequest
        currentRequest = null
        if (old) old.abort()
    }
    function reload() {
        retry.stop()
        cancelRequest()
        loadStarted()
        begin()
    }
    function failed() {
        cancelRequest()
        unavailable()
        retry.restart()
    }
    function get(path, callback) {
        var xhr = new XMLHttpRequest()
        currentRequest = xhr
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE || currentRequest !== xhr) return
            currentRequest = null
            watchdog.stop()
            if (xhr.status !== 200) { failed(); return }
            var data
            try { data = JSON.parse(xhr.responseText) } catch(e) { failed(); return }
            callback(data)
        }
        xhr.open("GET", baseUrl + path)
        watchdog.restart()
        xhr.send()
    }
    function begin() {
        get("/api/v1/config", function(config) {
            if (!config || typeof config !== "object") { failed(); return }
            get("/api/v1/experiences", function(experiences) {
                if (!Array.isArray(experiences)) { failed(); return }
                loaded(experiences, config.district || "", config.venue || "")
            })
        })
    }
    Timer { id: retry; interval: 2000; onTriggered: loader.begin() }
    Timer { id: watchdog; interval: 8000; onTriggered: loader.failed() }
    Component.onDestruction: cancelRequest()
}
