include(libkg/libkg.pri)

TEMPLATE = lib
TARGET = kg
QT += core qml quick network xml

target.path = /usr/share/ru.neochapay.samoletik/lib

INSTALLS = target
