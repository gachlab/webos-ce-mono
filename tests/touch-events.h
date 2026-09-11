// The synthetic touch events the tests send, built the same way on Qt 5 and 6.
//
// Qt 6 made a touch point read-only: QEventPoint is filled in through the
// private QMutableEventPoint, which is what Qt's own input code uses, and the
// device is a QPointingDevice. The tests describe the touch they want and this
// builds it.

#ifndef TESTS_TOUCH_EVENTS_H
#define TESTS_TOUCH_EVENTS_H

#include <QList>
#include <QPointF>
#include <QTouchEvent>

#include <QPointingDevice>
#include <QWindow>   // qeventpoint_p.h keeps a QPointer<QWindow>
#include <QtGui/private/qeventpoint_p.h>

namespace TestTouch {

// Whether the point also carries start and last positions equal to its
// current one.
enum History { NoHistory, WithHistory };

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


} // namespace TestTouch

#endif // TESTS_TOUCH_EVENTS_H
