QT += core gui testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = operator-pin-test
SOURCES += tst_operatorpin.cpp
HEADERS += ../../app/platform/kioskbridge.h
