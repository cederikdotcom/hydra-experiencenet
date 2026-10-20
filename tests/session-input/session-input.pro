QT += core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = session-input-test
SOURCES += tst_sessioninput.cpp ../../app/api/sessioninput.cpp
HEADERS += ../../app/api/sessioninput.h
win32:LIBS += -luser32
