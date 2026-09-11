// Prueba del camino de entrada en Qt5, sin levantar webOS.
//
// webOS solo registra un dedo cuando le llega un QEvent::TouchBegin a un
// QGraphicsItem (Page::sceneEvent -> touchStartEvent). Como Qt5 solo sintetiza
// toques desde eventos de raton que nadie acepto --- y QGraphicsView acepta el
// press --- WindowServer los construye a mano. Esto comprueba, aislado, si esa
// construccion a mano de verdad llega al item.
//
// Corre sin pantalla:  ./tactil-en-qt5 -platform offscreen
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QTouchEvent>
#include <QMouseEvent>
#include <cstdio>

static int vistos[3] = {0, 0, 0};   // begin, update, end

class ItemEspia : public QGraphicsObject {
public:
    ItemEspia() { setAcceptTouchEvents(true); }
    QRectF boundingRect() const override { return QRectF(0, 0, 400, 400); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
protected:
    bool sceneEvent(QEvent* e) override {
        switch (e->type()) {
        case QEvent::TouchBegin:  vistos[0]++; e->accept(); return true;
        case QEvent::TouchUpdate: vistos[1]++; e->accept(); return true;
        case QEvent::TouchEnd:    vistos[2]++; e->accept(); return true;
        default: break;
        }
        return QGraphicsObject::sceneEvent(e);
    }
};

// La misma traduccion que hace WindowServer::entregarComoToque.
static QTouchDevice* dispositivo()
{
    static QTouchDevice* d = 0;
    if (!d) {
        d = new QTouchDevice;
        d->setType(QTouchDevice::TouchScreen);
        d->setCapabilities(QTouchDevice::Position);
    }
    return d;
}

static bool enviarToque(QGraphicsView* vista, QEvent::Type tipo,
                        Qt::TouchPointState estado, const QPointF& p)
{
    QTouchEvent::TouchPoint punto(0);
    punto.setState(estado);
    punto.setPos(p);
    punto.setScenePos(p);
    punto.setScreenPos(p);
    punto.setLastPos(p);  punto.setLastScenePos(p);  punto.setLastScreenPos(p);
    punto.setStartPos(p); punto.setStartScenePos(p); punto.setStartScreenPos(p);
    punto.setPressure(estado == Qt::TouchPointReleased ? 0.0 : 1.0);

    QList<QTouchEvent::TouchPoint> puntos;
    puntos.append(punto);

    QTouchEvent toque(tipo, dispositivo(), Qt::NoModifier, estado, puntos);
    toque.setAccepted(false);
    QApplication::sendEvent(vista->viewport(), &toque);
    return toque.isAccepted();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QGraphicsScene escena(0, 0, 400, 400);
    ItemEspia* item = new ItemEspia;
    escena.addItem(item);

    QGraphicsView vista(&escena);
    vista.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    vista.resize(400, 400);
    vista.show();

    const QPointF p(100, 100);
    bool aBegin  = enviarToque(&vista, QEvent::TouchBegin,  Qt::TouchPointPressed,  p);
    bool aUpdate = enviarToque(&vista, QEvent::TouchUpdate, Qt::TouchPointMoved,    p);
    bool aEnd    = enviarToque(&vista, QEvent::TouchEnd,    Qt::TouchPointReleased, p);

    printf("TouchBegin  -> item:%d  aceptado:%d\n", vistos[0], aBegin);
    printf("TouchUpdate -> item:%d  aceptado:%d\n", vistos[1], aUpdate);
    printf("TouchEnd    -> item:%d  aceptado:%d\n", vistos[2], aEnd);

    bool bien = vistos[0] == 1 && vistos[1] == 1 && vistos[2] == 1;
    printf("%s\n", bien ? "OK: el toque construido a mano llega al item"
                        : "FALLO: el item no recibe el toque");
    return bien ? 0 : 1;
}
