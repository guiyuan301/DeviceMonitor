/****************************************************************************
** Meta object code from reading C++ file 'datamanager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.9.8)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/core/datamanager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'datamanager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_DataManager_t {
    QByteArrayData data[24];
    char stringdata0[260];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_DataManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_DataManager_t qt_meta_stringdata_DataManager = {
    {
QT_MOC_LITERAL(0, 0, 11), // "DataManager"
QT_MOC_LITERAL(1, 12, 13), // "deviceUpdated"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 2), // "id"
QT_MOC_LITERAL(4, 30, 11), // "alarmRaised"
QT_MOC_LITERAL(5, 42, 9), // "AlarmItem"
QT_MOC_LITERAL(6, 52, 4), // "item"
QT_MOC_LITERAL(7, 57, 13), // "alarmRestored"
QT_MOC_LITERAL(8, 71, 16), // "alarmListChanged"
QT_MOC_LITERAL(9, 88, 17), // "snapshotRequested"
QT_MOC_LITERAL(10, 106, 8), // "deviceId"
QT_MOC_LITERAL(11, 115, 6), // "reason"
QT_MOC_LITERAL(12, 122, 13), // "snapshotTaken"
QT_MOC_LITERAL(13, 136, 17), // "buzzerMuteChanged"
QT_MOC_LITERAL(14, 154, 12), // "onDeviceData"
QT_MOC_LITERAL(15, 167, 10), // "DeviceData"
QT_MOC_LITERAL(16, 178, 4), // "data"
QT_MOC_LITERAL(17, 183, 15), // "setDeviceOnline"
QT_MOC_LITERAL(18, 199, 2), // "on"
QT_MOC_LITERAL(19, 202, 11), // "addSnapshot"
QT_MOC_LITERAL(20, 214, 8), // "jpegPath"
QT_MOC_LITERAL(21, 223, 10), // "muteBuzzer"
QT_MOC_LITERAL(22, 234, 7), // "seconds"
QT_MOC_LITERAL(23, 242, 17) // "acknowledgeAlarms"

    },
    "DataManager\0deviceUpdated\0\0id\0alarmRaised\0"
    "AlarmItem\0item\0alarmRestored\0"
    "alarmListChanged\0snapshotRequested\0"
    "deviceId\0reason\0snapshotTaken\0"
    "buzzerMuteChanged\0onDeviceData\0"
    "DeviceData\0data\0setDeviceOnline\0on\0"
    "addSnapshot\0jpegPath\0muteBuzzer\0seconds\0"
    "acknowledgeAlarms"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_DataManager[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   74,    2, 0x06 /* Public */,
       4,    1,   77,    2, 0x06 /* Public */,
       7,    1,   80,    2, 0x06 /* Public */,
       8,    0,   83,    2, 0x06 /* Public */,
       9,    2,   84,    2, 0x06 /* Public */,
      12,    1,   89,    2, 0x06 /* Public */,
      13,    0,   92,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      14,    1,   93,    2, 0x0a /* Public */,
      17,    2,   96,    2, 0x0a /* Public */,
      19,    3,  101,    2, 0x0a /* Public */,
      21,    2,  108,    2, 0x0a /* Public */,
      23,    0,  113,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   10,   11,
    QMetaType::Void, QMetaType::Int,   10,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 15,   16,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    3,   18,
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::QString,   10,   20,   11,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,   10,   22,
    QMetaType::Void,

       0        // eod
};

void DataManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        DataManager *_t = static_cast<DataManager *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->deviceUpdated((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->alarmRaised((*reinterpret_cast< const AlarmItem(*)>(_a[1]))); break;
        case 2: _t->alarmRestored((*reinterpret_cast< const AlarmItem(*)>(_a[1]))); break;
        case 3: _t->alarmListChanged(); break;
        case 4: _t->snapshotRequested((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 5: _t->snapshotTaken((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->buzzerMuteChanged(); break;
        case 7: _t->onDeviceData((*reinterpret_cast< const DeviceData(*)>(_a[1]))); break;
        case 8: _t->setDeviceOnline((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 9: _t->addSnapshot((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 10: _t->muteBuzzer((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 11: _t->acknowledgeAlarms(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< AlarmItem >(); break;
            }
            break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< AlarmItem >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DeviceData >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (DataManager::*_t)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::deviceUpdated)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (DataManager::*_t)(const AlarmItem & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::alarmRaised)) {
                *result = 1;
                return;
            }
        }
        {
            typedef void (DataManager::*_t)(const AlarmItem & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::alarmRestored)) {
                *result = 2;
                return;
            }
        }
        {
            typedef void (DataManager::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::alarmListChanged)) {
                *result = 3;
                return;
            }
        }
        {
            typedef void (DataManager::*_t)(int , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::snapshotRequested)) {
                *result = 4;
                return;
            }
        }
        {
            typedef void (DataManager::*_t)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::snapshotTaken)) {
                *result = 5;
                return;
            }
        }
        {
            typedef void (DataManager::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DataManager::buzzerMuteChanged)) {
                *result = 6;
                return;
            }
        }
    }
}

const QMetaObject DataManager::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_DataManager.data,
      qt_meta_data_DataManager,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *DataManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DataManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DataManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DataManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void DataManager::deviceUpdated(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void DataManager::alarmRaised(const AlarmItem & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void DataManager::alarmRestored(const AlarmItem & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void DataManager::alarmListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void DataManager::snapshotRequested(int _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void DataManager::snapshotTaken(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void DataManager::buzzerMuteChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
