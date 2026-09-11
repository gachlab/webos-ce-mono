// The synthetic touch events the tests send, built the same way on Qt 5 and 6.
//
// Qt 5 filled a QTouchEvent::TouchPoint through its setters and needed a
// QTouchDevice. Qt 6 made the point read-only (QEventPoint, filled in through
// the private QMutableEventPoint, which is what Qt's own input code uses) and
// replaced the device with QPointingDevice. The tests describe the touch they
// want once and this header builds it for whichever Qt they are compiled with.

#ifndef TESTS_TOUCH_EVENTS_H
#define TESTS_TOUCH_EVENTS_H

#include <QList>
#include <QPointF>
#include <QTouchEvent>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPointingDevice>
#include <QWindow>   // qeventpoint_p.h keeps a QPointer<QWindow>
#include <QtGui/private/qeventpoint_p.h>
#endif

namespace TestTouch {

// Whether the point also carries start and last positions equal to its
// current one. Qt 5 left them at zero unless set.
enum History { NoHistory, WithHistory };

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

inline const QPointingDevice* device()
{
    static const QPointingDevice* d = new QPointingDevice(
        QStringLiteral("test-touchscreen"), 0, QInputDevice::DeviceType::TouchScreen,
        QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 1, 0);
    return d;
}

// Position, scene position and screen position all at p, as for a view at the
// origin of an unscrolled scene. A negative pressure leaves Qt's default.
inline QTouchEvent::TouchPoint point(int id, Qt::TouchPointState state, const QPointF& p,
                                     History history = NoHistory, qreal pressure = -1)
{
    QEventPoint pt = QMutableEventPoint::withTimeStamp(0, id, QEventPoint::State(state), p, p, p);
    if (history == WithHistory) {
        // Qt 6 derives the local and scene start and last positions from these.
        QMutableEventPoint::setGlobalPressPosition(pt, p);
        QMutableEventPoint::setGlobalLastPosition(pt, p);
    }
    if (pressure >= 0)
        QMutableEventPoint::setPressure(pt, pressure);
    return pt;
}

inline QTouchEvent event(QEvent::Type type, Qt::TouchPointState state,
                         const QList<QTouchEvent::TouchPoint>& points)
{
    Q_UNUSED(state);   // Qt 6 reads the states from the points themselves
    return QTouchEvent(type, device(), Qt::NoModifier, points);
}

#else

inline QTouchDevice* device()
{
    static QTouchDevice* d = nullptr;
    if (!d) {
        d = new QTouchDevice;
        d->setType(QTouchDevice::TouchScreen);
        d->setCapabilities(QTouchDevice::Position);
    }
    return d;
}

inline QTouchEvent::TouchPoint point(int id, Qt::TouchPointState state, const QPointF& p,
                                     History history = NoHistory, qreal pressure = -1)
{
    QTouchEvent::TouchPoint pt(id);
    pt.setState(state);
    pt.setPos(p);
    pt.setScenePos(p);
    pt.setScreenPos(p);
    if (history == WithHistory) {
        pt.setLastPos(p);  pt.setLastScenePos(p);  pt.setLastScreenPos(p);
        pt.setStartPos(p); pt.setStartScenePos(p); pt.setStartScreenPos(p);
    }
    if (pressure >= 0)
        pt.setPressure(pressure);
    return pt;
}

inline QTouchEvent event(QEvent::Type type, Qt::TouchPointState state,
                         const QList<QTouchEvent::TouchPoint>& points)
{
    return QTouchEvent(type, device(), Qt::NoModifier, state, points);
}

#endif

} // namespace TestTouch

#endif // TESTS_TOUCH_EVENTS_H
