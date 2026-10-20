#include <QtTest>
#include "../../app/platform/kioskbridge.h"

class OperatorPinTest : public QObject {
    Q_OBJECT
private slots:
    void pinGate() {
        const QString path = QCoreApplication::applicationDirPath() + "/operator-pin.sha256";
        QVERIFY(!QFile::exists(path));
        KioskBridge unconfigured;
        QVERIFY(!unconfigured.operatorPinRequired());
        QVERIFY(!unconfigured.verifyOperatorPin("4826"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QCryptographicHash::hash("4826", QCryptographicHash::Sha256).toHex());
        file.close();
        KioskBridge configured;
        QVERIFY(configured.operatorPinRequired());
        QVERIFY(!configured.verifyOperatorPin("0000"));
        QVERIFY(!configured.verifyOperatorPin("4826")); // throttled, even correct
        QTest::qWait(2100);
        QVERIFY(configured.verifyOperatorPin("4826"));
        QVERIFY(QFile::remove(path));
        QVERIFY(configured.operatorPinRequired()); // cannot unlock by deleting
        QTest::qWait(2100);
        QVERIFY(!configured.verifyOperatorPin("4826"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("malformed");
        file.close();
        KioskBridge malformed;
        QVERIFY(malformed.operatorPinRequired());
        QVERIFY(!malformed.verifyOperatorPin("4826"));
        QVERIFY(QFile::remove(path));
    }
};
QTEST_GUILESS_MAIN(OperatorPinTest)
#include "tst_operatorpin.moc"
