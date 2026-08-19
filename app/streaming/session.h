#pragma once

#include <QSemaphore>
#include <QQuickWindow>

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"

class SupportedVideoFormatList : public QList<int>
{
public:
    operator int() const
    {
        int value = 0;

        for (const int v : *this) {
            value |= v;
        }

        return value;
    }

    void
    removeByMask(int mask)
    {
        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                this->removeAt(i);
            }
            else {
                i++;
            }
        }
    }

    void
    deprioritizeByMask(int mask)
    {
        QList<int> deprioritizedList;

        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                deprioritizedList.append(this->takeAt(i));
            }
            else {
                i++;
            }
        }

        this->append(std::move(deprioritizedList));
    }

    int maskByServerCodecModes(int serverCodecModes)
    {
        int mask = 0;

        const QMap<int, int> mapping = {
            {SCM_H264, VIDEO_FORMAT_H264},
            {SCM_H264_HIGH8_444, VIDEO_FORMAT_H264_HIGH8_444},
            {SCM_HEVC, VIDEO_FORMAT_H265},
            {SCM_HEVC_MAIN10, VIDEO_FORMAT_H265_MAIN10},
            {SCM_HEVC_REXT8_444, VIDEO_FORMAT_H265_REXT8_444},
            {SCM_HEVC_REXT10_444, VIDEO_FORMAT_H265_REXT10_444},
            {SCM_AV1_MAIN8, VIDEO_FORMAT_AV1_MAIN8},
            {SCM_AV1_MAIN10, VIDEO_FORMAT_AV1_MAIN10},
            {SCM_AV1_HIGH8_444, VIDEO_FORMAT_AV1_HIGH8_444},
            {SCM_AV1_HIGH10_444, VIDEO_FORMAT_AV1_HIGH10_444},
        };

        for (QMap<int, int>::const_iterator it = mapping.cbegin(); it != mapping.cend(); ++it) {
            if (serverCodecModes & it.key()) {
                mask |= it.value();
                serverCodecModes &= ~it.key();
            }
        }

        // Make sure nobody forgets to update this for new SCM values
        SDL_assert(serverCodecModes == 0);

        int val = *this;
        return val & mask;
    }
};

class Session : public QObject
{
    Q_OBJECT

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;

public:
    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);
    virtual ~Session();

    Q_INVOKABLE bool initialize(QQuickWindow* qtWindow);
    Q_INVOKABLE void start();
    Q_INVOKABLE void interrupt();
    Q_PROPERTY(QStringList launchWarnings MEMBER m_LaunchWarnings NOTIFY launchWarningsChanged);

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    void flushWindowEvents();

    void setShouldExit(bool quitHostApp = false);

    // Shows the subtle circle handle at the top-right of the stream so
    // visitors can discover how to return to the experience grid.
    void showExitOverlay();

    // Tests whether a window-space point lies inside the circle handle's
    // hit region. Returns true when the handle is visible and the point
    // is within its clickable rectangle.
    bool isPointInExitOverlay(int windowX, int windowY);

    // Triggered when the visitor taps the circle handle. Toggles the
    // dropdown menu between hidden and visible states.
    void triggerExitFromOverlay();

    // Tests whether a window-space point lies inside the expanded menu's
    // "Exit experience" item. Only true when the menu is currently open.
    bool isPointInExitMenu(int windowX, int windowY);

    // Triggered when the visitor taps the "Exit experience" item in the
    // expanded menu. Hides overlays and asks the session to disconnect
    // cleanly so StreamSegue returns to the kiosk grid. Exposed to QML
    // so StreamOverlay.qml can invoke it directly on click.
    Q_INVOKABLE void triggerExitFromMenu();

    // Reports whether the dropdown menu is currently in its open state.
    // Used by the input layer to know whether a tap outside the menu
    // should collapse it.
    bool isExitMenuOpen() const { return m_ExitMenuOpen; }

    // Collapses the dropdown menu back to just the circle handle.
    void closeExitMenu();

    // Scene mode: the Qt Quick scene graph renders the decoded video
    // inside the existing kiosk window through QuickSinkBridge, so no
    // SDL window is created and the SDL event loop never runs. Must be
    // set before start(). In scene mode exec() is an async start: it
    // creates the decoder and returns immediately, leaving the Qt event
    // loop running. The stream ends through stopSession(). Linux only;
    // the setter ignores enable requests on other platforms.
    Q_INVOKABLE void setSceneMode(bool enabled);
    Q_INVOKABLE bool isSceneMode() const { return m_SceneMode; }

    // Thread-safe request to recreate the video decoder in scene mode.
    // This is the analog of pushing SDL_RENDER_DEVICE_RESET on the SDL
    // path: it emits a queued signal so the recreation happens on the
    // main thread. Callable from any thread. No-op outside scene mode.
    void requestSceneDecoderReset();

public slots:
    // Scene-mode analog of the DispatchDeferredCleanup block in exec():
    // destroys the decoder under the decoder lock, disables the frame
    // bridge, then dispatches DeferredSessionCleanupTask exactly as the
    // SDL path does (which stops the connection and emits
    // sessionFinished). Idempotent; only the first call does the work.
    // Callable from QML to end a scene-mode stream. No-op outside scene
    // mode.
    void stopSession();

signals:
    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void displayLaunchError(QString text);

    void quitStarting();

    void sessionFinished(int portTestResult);

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();

    void launchWarningsChanged();

    // Scene-mode internal routing. These replace the SDL event queue as
    // the cross-thread hop to the main thread: the connection listener
    // thread emits sceneConnectionTerminated instead of pushing SDL_QUIT
    // and the decoder path emits sceneDecoderResetRequested instead of
    // pushing SDL_RENDER_DEVICE_RESET. Both are connected with queued
    // connections in setSceneMode().
    void sceneConnectionTerminated(int errorCode);
    void sceneDecoderResetRequested();

private slots:
    // Scene-mode handlers for the signals above; both run on the main
    // thread via queued connections.
    void handleSceneConnectionTermination(int errorCode);
    void handleSceneDecoderReset();

private:
    void exec();

    bool startConnectionAsync();

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool initializeAudioRenderer();

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen();

    void notifyMouseEmulationMode(bool enabled);

    void updateOptimalWindowDisplayMode();

    enum class DecoderAvailability {
        None,
        Software,
        Hardware
    };

    static
    DecoderAvailability getDecoderAvailability(SDL_Window* window,
                                               StreamingPreferences::VideoDecoderSelection vds,
                                               int videoFormat, int width, int height, int frameRate);

    // The trailing sceneMode parameter defaults to false so all existing
    // call sites are unchanged. Scene-mode callers pass true (with a null
    // window) so decoder selection picks the scene sink renderer instead
    // of an SDL-window-backed one.
    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool testOnly,
                       IVideoDecoder*& chosenDecoder,
                       bool sceneMode = false);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

    static
    void clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger);

    static
    void clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

    static
    void clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

    static
    void clSetAdaptiveTriggers(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, uint8_t *left, uint8_t *right);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    SupportedVideoFormatList m_SupportedVideoFormats; // Sorted in order of descending priority
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    SDL_Window* m_Window;
    IVideoDecoder* m_VideoDecoder;
    SDL_mutex* m_DecoderLock;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    QQuickWindow* m_QtWindow;
    bool m_UnexpectedTermination;
    SdlInputHandler* m_InputHandler;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;
    QStringList m_LaunchWarnings;
    bool m_ShouldExit;

    bool m_AsyncConnectionSuccess;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig;
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;
    bool m_ExitMenuOpen = false;

    // Scene-mode state. m_SceneMode is only ever set true by the Linux
    // kiosk stream page; it stays false everywhere else so the SDL path
    // is untouched on all other platforms. m_SceneCleanupDone guards
    // stopSession() against running cleanup twice.
    bool m_SceneMode = false;
    bool m_SceneCleanupDone = false;

    // True once exec() has entered its scene-mode branch (the async
    // connection succeeded and the stream is live). Before that point
    // stopSession() must not tear the connection down itself: the
    // connection thread may still be inside LiStartConnection(), which
    // only LiInterruptConnection() may interrupt safely. exec()'s
    // failure branch then dispatches the cleanup exactly once.
    bool m_SceneStreamStarted = false;

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
