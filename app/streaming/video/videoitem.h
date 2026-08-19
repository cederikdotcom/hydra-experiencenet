#pragma once

#include <QQuickItem>

// VideoItem renders scene-mode video frames inside the Qt Quick scene graph
// (issue #507). It consumes frames from QuickSinkBridge and draws them with
// raw OpenGL through a QSGRenderNode, so no precompiled qsb shaders are
// needed. The video is letterboxed to preserve the stream aspect ratio
// inside the item rectangle.
//
// Software frames upload through the M1 copy path. AV_PIX_FMT_VAAPI frames
// (M2) import zero-copy: one EGLImage per plane binds to plain 2D textures
// feeding the same shaders. Separate-layer backends provide the plane
// images through exportEGLImages(); composed-layer backends (i965) provide
// the raw composed NV12 dmabuf through mapDrmPrimeFrame() and the item
// imports the R8/GR88 planes itself with per-plane offset and pitch (issue
// #507 M0 amendment). On any import failure the item logs once, flips the
// bridge to software readback for the rest of the session, and transfers
// the failing frame one-off so the stream does not glitch.
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
