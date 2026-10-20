"""Exercise real QML against a fake head; never calls an OS power command."""
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from PySide6.QtCore import QObject, Slot, Signal, QUrl, QMetaObject, Q_ARG
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent, qmlRegisterSingletonInstance, qmlRegisterType
from PySide6.QtTest import QTest

requests = []
power_calls = []
config_attempts = 0
catalog_attempts = 0
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def reply(self, status, body):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(body.encode())
    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        requests.append(data['action'])
        if data['pin'] != '4826':
            self.reply(403, '{"error":"Incorrect PIN"}')
        else:
            power_calls.append(data['action'])
            self.reply(202, '{"scheduled":true}')
    def do_GET(self):
        global config_attempts, catalog_attempts
        if self.path.endswith('/config'):
            config_attempts += 1
            self.reply(503 if config_attempts == 1 else 200,
                       '{"district":"test","venue":"kiosk"}')
        else:
            catalog_attempts += 1
            self.reply(200, 'invalid-json' if catalog_attempts == 1 else '[{"name":"game"}]')

class Bridge(QObject):
    enabled = True
    @Slot(result=bool)
    def operatorPinRequired(self): return self.enabled
    @Slot(result=bool)
    def operatorPowerAvailable(self): return self.enabled
    @Slot(str, result=bool)
    def verifyOperatorPin(self, pin):
        raise AssertionError('Power must authenticate at the head, not this UI verifier')
    @Slot(QObject)
    def makeFollowAllSpaces(self, window): pass
class Session(QObject): pass
class LocalServer(QObject):
    kioskWindowShown = Signal()

app = QGuiApplication([])
b = Bridge()
qmlRegisterSingletonInstance(Bridge, 'KioskBridge', 1, 0, 'KioskBridge', b)
qmlRegisterType(Session, 'Session', 1, 0, 'Session')
engine = QQmlEngine()
local = LocalServer()
engine.rootContext().setContextProperty('localServerBridge', local)
root = Path(__file__).resolve().parents[2] / 'app/gui'
components = []
def create(name):
    c = QQmlComponent(engine, QUrl.fromLocalFile(str(root / name)))
    assert not c.isError(), '\n'.join(e.toString() for e in c.errors())
    o = c.create()
    assert o, '\n'.join(e.toString() for e in c.errors())
    components.append(c)
    return o

def wait_for(predicate, timeout=7000):
    for _ in range(timeout // 20):
        if predicate(): return
        QTest.qWait(20)
    assert predicate(), 'Timed out'

# Start with no listening head: the catalog must recover without UI restart.
loader = create('KioskCatalogLoader.qml')
loaded = []
loader.loaded.connect(lambda items, district, venue: loaded.append((district, venue)))
QMetaObject.invokeMethod(loader, 'reload')
QTest.qWait(100)
server = ThreadingHTTPServer(('127.0.0.1', 9740), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
wait_for(lambda: bool(loaded), 9000)
assert loaded == [('test', 'kiosk')] and config_attempts >= 3 and catalog_attempts == 2

w = create('StreamOverlay.qml')
w.setProperty('visible', True)
pin = w.findChild(QObject, 'operatorPinInput')
assert pin
prompt = pin.parent().parent()
def request(action):
    QMetaObject.invokeMethod(prompt, 'requestAction', Q_ARG('QVariant', action))
    app.processEvents()

def submit(pin_text):
    pin.setProperty('text', pin_text)
    QMetaObject.invokeMethod(prompt, 'submit')
    app.processEvents()

request('restart')
submit('0000')
wait_for(lambda: not prompt.property('submitting'))
assert prompt.property('visible') and pin.property('text') == '' and not power_calls
QMetaObject.invokeMethod(prompt, 'cancel')
assert not prompt.property('visible') and not power_calls
request('shutdown')
QTest.qWait(2200)
submit('4826')
# A concurrent menu-return signal must not dismiss a submitted power request.
local.kioskWindowShown.emit()
wait_for(lambda: not prompt.property('submitting'))
assert power_calls == ['shutdown'] and w.property('powerAction') == 'shutdown'
assert w.property('quitting') and not prompt.property('visible')
w.setProperty('powerAction', '')
local.kioskWindowShown.emit()
request('restart')
submit('4826')
wait_for(lambda: w.property("powerAction") == "restart")
assert power_calls == ['shutdown', 'restart']
assert w.property('powerAction') == 'restart'
# An unconfigured kiosk must not bypass a PIN to run a power action.
w.setProperty('powerAction', '')
local.kioskWindowShown.emit()
b.enabled = False
request('shutdown')
assert not prompt.property('visible') and len(power_calls) == 2
server.shutdown()
w.setProperty('visible', False)
print('PASS: unavailable/malformed head responses recover; wrong PIN and Cancel never schedule power; both actions require head authorization; unconfigured power stays closed.')
