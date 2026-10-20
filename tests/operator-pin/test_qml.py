import os
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
from PySide6.QtCore import QObject,Slot,Signal,QUrl,Property,QMetaObject
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine,QQmlComponent,qmlRegisterSingletonInstance,qmlRegisterType
from PySide6.QtTest import QTest
class Bridge(QObject):
    @Slot(result=bool)
    def operatorPinRequired(self): return True
    @Slot(result=bool)
    def operatorPowerAvailable(self): return True
    @Slot(str,result=bool)
    def verifyOperatorPin(self,pin): return pin=='4826'
    @Slot(QObject)
    def makeFollowAllSpaces(self,w): pass
class Session(QObject):
    exits=0
    @Slot()
    def triggerExitFromMenu(self): self.exits+=1
class LocalServer(QObject):
    kioskWindowShown=Signal()
app=QGuiApplication([]); b=Bridge()
qmlRegisterSingletonInstance(Bridge,'KioskBridge',1,0,'KioskBridge',b)
qmlRegisterType(Session,'Session',1,0,'Session')
engine=QQmlEngine()
server=LocalServer()
engine.rootContext().setContextProperty('localServerBridge',server)
c=QQmlComponent(engine,QUrl.fromLocalFile(str(Path(__file__).resolve().parents[2] / 'app/gui/StreamOverlay.qml')))
assert not c.isError(), '\n'.join(e.toString() for e in c.errors())
w=c.create(); assert w, '\n'.join(e.toString() for e in c.errors())
s=Session(); w.setProperty('session',s);w.setProperty('visible',True)
pin=w.findChild(QObject,'operatorPinInput'); assert pin
prompt=pin.parent().parent();assert prompt.metaObject().indexOfMethod('request()')>=0
QMetaObject.invokeMethod(prompt,'request');app.processEvents()
assert prompt.property('visible')
pin.setProperty('text','0000');QMetaObject.invokeMethod(prompt,'submit');app.processEvents()
assert prompt.property('visible') and s.exits==0 and pin.property('text')==''
QMetaObject.invokeMethod(prompt,'cancel');app.processEvents()
assert not prompt.property('visible') and s.exits==0 and w.width()==220
QMetaObject.invokeMethod(prompt,'request');app.processEvents()
pin.setProperty('text','1234')
server.kioskWindowShown.emit();app.processEvents()
assert not prompt.property('visible') and pin.property('text')==''
assert s.exits==0 and w.width()==220 and not w.property('quitting')
QMetaObject.invokeMethod(prompt,'request');QTest.qWait(2200)
pin.setProperty('text','4826');QMetaObject.invokeMethod(prompt,'submit');app.processEvents()
assert not prompt.property('visible') and s.exits==1 and w.property('quitting')
server.kioskWindowShown.emit();app.processEvents()
assert s.exits==1 and not w.property('quitting') and w.width()==220
print('PASS: wrong PIN and Cancel preserve session; agent menu return clears a pending PIN and exit veil without authorizing an exit; correct PIN exits exactly once.')
w.setProperty('visible',False);del w;del c;del engine
