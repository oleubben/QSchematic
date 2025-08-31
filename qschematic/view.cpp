#include <QKeyEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>

#include "commands/item_remove.hpp"
#include "scene.hpp"
#include "settings.hpp"
#include "view.hpp"

using namespace QSchematic;

View::View(QWidget *parent) : QGraphicsView(parent) {

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Interaction stuff
    setMouseTracking(true);
    setAcceptDrops(true);
    setDragMode(QGraphicsView::RubberBandDrag);

    //  Rendering options
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // Set initial zoom value
    setZoomValue(1.0);
}

void View::keyPressEvent(QKeyEvent *event) {
    // Something with CTRL held down?
    if (event->modifiers() & Qt::ControlModifier) {

        switch (event->key()) {
        case Qt::Key_Plus:
            _scaleFactor += zoom_factor_step;
            updateScale();
            return;

        case Qt::Key_Minus:
            _scaleFactor -= zoom_factor_step;
            updateScale();
            return;

        case Qt::Key_0:
            setZoomValue(1.0);
            updateScale();
            return;

        case Qt::Key_W:
            if (_scene)
                _scene->setMode(Scene::WireMode);
            return;

        case Qt::Key_Space:
            if (_scene)
                _scene->toggleWirePosture();
            return;

        default:
            break;
        }
    }

    // Just a key alone?
    switch (event->key()) {
    case Qt::Key_Escape:
        if (_scene)
            _scene->setMode(Scene::NormalMode);
        return;

    case Qt::Key_Delete:
        if (_scene) {
            if (_scene->mode() == Scene::NormalMode) {
                for (auto item : _scene->selectedTopLevelItems())
                    _scene->undoStack()->push(
                        new Commands::ItemRemove(_scene, item));
            } else
                _scene->removeLastWirePoint();
        }
        return;

    case Qt::Key_Backspace:
        if (_scene && _scene->mode() == Scene::WireMode)
            _scene->removeLastWirePoint();
        else
            QGraphicsView::keyPressEvent(event);

        return;

    default:
        break;
    }

    // Fall back
    QGraphicsView::keyPressEvent(event);
}

void View::wheelEvent(QWheelEvent *event) {
    // CTRL + wheel to zoom
    if (event->modifiers() & Qt::ControlModifier) {

        // Zoom in (clip)
        if (event->angleDelta().y() > 0)
            _scaleFactor += zoom_factor_step;

        // Zoom out (clip)
        else if (event->angleDelta().y() < 0)
            _scaleFactor -= zoom_factor_step;

        _scaleFactor = qBound(0.0, _scaleFactor, 1.0);

        updateScale();
    }
}
void View::mouseMoveEvent(QMouseEvent *event) {
    QGraphicsView::mouseMoveEvent(event);

    switch (_mode) {
    case NormalMode:
        break;

    case PanMode:
        // Pan Movement
        auto delta = mapToScene(event->pos()) - mapToScene(_panStart.toPoint());
        setTransformationAnchor(QGraphicsView::NoAnchor);
        setTransform(transform().translate(delta.x(), delta.y()));
        _panStart = event->position();
        event->accept();
        updateSceneRect();
        return;
    }
}

void View::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        setMode(PanMode);
        _panStart = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void View::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        setMode(NormalMode);
        viewport()->setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void View::setScene(Scene *scene) {
    if (scene) {
        // Change cursor depending on scene mode
        connect(scene, &Scene::modeChanged, [this](int newMode) {
            switch (newMode) {
            case Scene::NormalMode:
                viewport()->setCursor(Qt::ArrowCursor);
                break;

            case Scene::WireMode:
                viewport()->setCursor(Qt::CrossCursor);
                break;

            default:
                break;
            }
        });
    }

    QGraphicsView::setScene(scene);

    _scene = scene;
}

void View::setSettings(const Settings &settings) {
    _settings = settings;

    // Rendering options
    setRenderHint(QPainter::Antialiasing, _settings.antialiasing);
}

void View::setZoomValue(qreal factor) {
    _scaleFactor =
        qLn(zoom_factor_min / factor) / qLn(zoom_factor_min / zoom_factor_max);

    updateScale();
}

void View::updateScale() {
    // Exponential interpolation
    float logMinZoom = qLn(zoom_factor_min);
    float logMaxZoom = qLn(zoom_factor_max);
    float logZoom = logMinZoom + (logMaxZoom - logMinZoom) * _scaleFactor;
    float zoom = qExp(logZoom);

    // Apply the new scale
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setTransform(QTransform::fromScale(zoom, zoom));
    updateSceneRect();
    qDebug() << zoom;
    Q_EMIT zoomChanged(zoom);
}

void QSchematic::View::updateSceneRect() {
    QRectF rect = mapToScene(viewport()->rect()).boundingRect().toRect();
    rect = QRectF(rect.x() - 50, rect.y() - 50, rect.width() + 100,
                  rect.height() + 100);
    if (!sceneRect().toRect().contains(rect.toRect(), true)) {
        rect = sceneRect().united(rect);
        setSceneRect(rect);
    }
}

void View::setMode(const Mode newMode) {
    _mode = newMode;

    Q_EMIT modeChanged(_mode);
}

qreal View::zoomValue() const { return _scaleFactor; }

void View::fitInView() {
    // Sanity check
    if (!_scene)
        return;

    // Find the combined bounding rect of all the items
    QRectF rect;
    for (const auto &item : _scene->QGraphicsScene::items()) {
        QRectF boundingRect = item->boundingRect();
        boundingRect.moveTo(item->scenePos());
        rect = rect.united(boundingRect);
    }

    // Add some padding
    const auto adj = std::max(0.0, fitall_padding);
    rect.adjust(-adj, -adj, adj, adj);

    // Update and cap the scale factor
    qreal currentScaleFactor = _scaleFactor;
    qreal newScaleFactor = _scaleFactor;
    QGraphicsView::fitInView(rect, Qt::KeepAspectRatio);
    newScaleFactor = viewport()->geometry().width() /
                     mapToScene(viewport()->geometry()).boundingRect().width();
    if (currentScaleFactor < 1)
        newScaleFactor = std::min(newScaleFactor, 1.0);
    else
        newScaleFactor = std::min(newScaleFactor, currentScaleFactor);
    setZoomValue(newScaleFactor);
}
