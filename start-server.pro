QT += widgets
CONFIG += c++17 release
TARGET = start-server
TEMPLATE = app
SOURCES += start-server.cpp
RC_FILE = start-server.rc
LIBS += -lshell32
