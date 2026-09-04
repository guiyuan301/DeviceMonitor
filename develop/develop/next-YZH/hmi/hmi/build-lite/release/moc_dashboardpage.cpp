/****************************************************************************
** Meta object code from reading C++ file 'dashboardpage.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.9.8)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/pages/dashboardpage.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'dashboardpage.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_DashboardPage_t {
    QByteArrayData data[10];
    char stringdata0[107];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_DashboardPage_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_DashboardPage_t qt_meta_stringdata_DashboardPage = {
    {
QT_MOC_LITERAL(0, 0, 13), // "DashboardPage"
QT_MOC_LITERAL(1, 14, 15), // "onDeviceUpdated"
QT_MOC_LITERAL(2, 30, 0), // ""
QT_MOC_LITERAL(3, 31, 2), // "id"
QT_MOC_LITERAL(4, 34, 18), // "onAlarmListChanged"
QT_MOC_LITERAL(5, 53, 16), // "onDeviceSelected"
QT_MOC_LITERAL(6, 70, 7), // "onBlink"
QT_MOC_LITERAL(7, 78, 10), // "onModeTemp"
QT_MOC_LITERAL(8, 89, 10), // "onModeHumi"
QT_MOC_LITERAL(9, 100, 6) // "onMute"

    },
    "DashboardPage\0onDeviceUpdated\0\0id\0"
    "onAlarmListChanged\0onDeviceSelected\0"
    "onBlink\0onModeTemp\0onModeHumi\0onMute"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_DashboardPage[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x08 /* Private */,
       4,    0,   52,    2, 0x08 /* Private */,
       5,    1,   53,    2, 0x08 /* Private */,
       6,    0,   56,    2, 0x08 /* Private */,
       7,    0,   57,    2, 0x08 /* Private */,
       8,    0,   58,    2, 0x08 /* Private */,
       9,    0,   59,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void DashboardPage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        DashboardPage *_t = static_cast<DashboardPage *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->onDeviceUpdated((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->onAlarmListChanged(); break;
        case 2: _t->onDeviceSelected((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->onBlink(); break;
        case 4: _t->onModeTemp(); break;
        case 5: _t->onModeHumi(); break;
        case 6: _t->onMute(); break;
        default: ;
        }
    }
}

const QMetaObject DashboardPage::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_DashboardPage.data,
      qt_meta_data_DashboardPage,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *DashboardPage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DashboardPage::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DashboardPage.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int DashboardPage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
