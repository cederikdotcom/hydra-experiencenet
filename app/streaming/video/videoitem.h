#pragma once

#include <QQuickItem>

// VideoItem renders scene-mode video frames inside the Qt Quick scene graph
// (issue #507 M1, copy path). It consumes software frames from
// QuickSinkBridge and draws them with raw OpenGL through a QSGRenderNode,
// so no precompiled qsb shaders are needed. The video is letterboxed to
// preserve the stream aspect ratio inside the item rectangle.
//
// Requirements: the Qt scene graph must be running on the OpenGL RHI
// backend (desktop GL or GLES 3). This is the case on the Linux kiosk,
// which is the only place scene mode is ever enabled.
//
// QML registration is done by the caller (kiosk wiring), not here.
class VideoItem : public QQuickItem
{
    Q_OBJECT

public:
    explicit VideoItem(QQuickItem* parent = nullptr);

signals:
    // Emitted once per item lifetime when the first decoded frame is
    // handed to the scene graph. The kiosk stream page uses it to drop
    // its loading veil at the right moment.
    void firstFrameReceived();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData) override;

private slots:
    void onFrameReady();

private:
    bool m_FirstFrameSeen = false;
};
