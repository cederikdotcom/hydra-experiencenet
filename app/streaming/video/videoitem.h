#pragma once

#include <QQuickItem>

class QuickInputHandler;

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
// Input (M3): while streamActive is true the item forwards mouse, touch,
// wheel and keyboard events to a QuickInputHandler, which translates them
// into Limelight calls, and hides the local cursor so exactly one cursor
// (the host's) is visible. Pointer coordinates are normalized against
// videoRect(), which uses the same letterbox math as the render node.
//
// Requirements: the Qt scene graph must be running on the OpenGL RHI
// backend (desktop GL or GLES 3). This is the case on the Linux kiosk,
// which is the only place scene mode is ever enabled.
//
// QML registration is done by the caller (kiosk wiring), not here.
class VideoItem : public QQuickItem
{
    Q_OBJECT

    // True while the stream is connected. Gates all input forwarding and
    // hides the local cursor (issue #507 review finding 1: exactly one
    // cursor visible during a stream, the host's). The kiosk stream page
    // sets it on connectionStarted and clears it on quitStarting, on
    // sessionFinished and when the page deactivates; clearing it raises
    // all keys and cancels active pointers so nothing sticks on the host.
    Q_PROPERTY(bool streamActive READ streamActive WRITE setStreamActive NOTIFY streamActiveChanged)

public:
    explicit VideoItem(QQuickItem* parent = nullptr);
    ~VideoItem() override;

    bool streamActive() const { return m_StreamActive; }
    void setStreamActive(bool active);

    // The letterboxed video rectangle in item-local coordinates, derived
    // from the latest frame's dimensions. Must stay identical to the
    // letterbox math in VideoRenderNode::render() (videoitem.cpp) so
    // pointer coordinates and rendered pixels agree exactly. Invalid
    // (empty) before the first frame.
    QRectF videoRect() const;

signals:
    // Emitted once per item lifetime when the first decoded frame is
    // handed to the scene graph. The kiosk stream page uses it to drop
    // its loading veil at the right moment.
    void firstFrameReceived();

    void streamActiveChanged();

    // Re-emitted from QuickInputHandler when the Ctrl+Alt+Shift+Q quit
    // combo fires. The kiosk stream page connects it to stopSession().
    void quitRequested();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData) override;

    // Input forwarding (issue #507 M3)
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent* event) override;
    void touchEvent(QTouchEvent* event) override;
    void touchUngrabEvent() override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private slots:
    void onFrameReady();

private:
    bool m_FirstFrameSeen = false;

    // Input state (issue #507 M3)
    QuickInputHandler* m_InputHandler;
    bool m_StreamActive = false;
    QSize m_FrameSize;
    QMetaObject::Connection m_WindowActiveConnection;
};
