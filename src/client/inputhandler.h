#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QQueue>
#include <QtCore/QThread>

// 输入事件类型
enum class InputEventType {
    MouseMove,
    MousePress,
    MouseRelease,
    MouseWheel,
    KeyPress,
    KeyRelease
};

// 输入事件结构
struct InputEvent {
    InputEventType type;
    QPoint position;
    int button;
    int key;
    int modifiers;
    int wheelDelta;
    QString text;
    qint64 timestamp;
    
    InputEvent()
        : type(InputEventType::MouseMove)
        , button(0)
        , key(0)
        , modifiers(0)
        , wheelDelta(0)
        , timestamp(0)
    {}
};

class InputHandler : public QObject
{
    Q_OBJECT
    
public:
    explicit InputHandler(QObject *parent = nullptr);
    ~InputHandler();
    
    // 输入处理控制
    void setEnabled(bool enabled);
    bool isEnabled() const;
    
    void setMouseEnabled(bool enabled);
    bool isMouseEnabled() const;
    
    void setKeyboardEnabled(bool enabled);
    bool isKeyboardEnabled() const;
    
    // 输入事件处理
    void handleMouseMove(const QPoint &position);
    void handleMousePress(const QPoint &position, Qt::MouseButton button);
    void handleMouseRelease(const QPoint &position, Qt::MouseButton button);
    void handleMouseWheel(const QPoint &position, int delta);
    
    void handleKeyPress(int key, Qt::KeyboardModifiers modifiers, const QString &text = QString());
    void handleKeyRelease(int key, Qt::KeyboardModifiers modifiers);
    
    // 批量处理
    void handleInputEvents(const QList<InputEvent> &events);
    
    // 输入过滤
    void setMouseFilter(bool enable);
    bool mouseFilter() const;
    
    void setKeyboardFilter(bool enable);
    bool keyboardFilter() const;
    
    void addFilteredKey(int key);
    void removeFilteredKey(int key);
    void clearFilteredKeys();
    QList<int> filteredKeys() const;
    
    // 输入延迟和缓冲
    void setInputDelay(int msecs);
    int inputDelay() const;
    
    void setBufferSize(int size);
    int bufferSize() const;
    
    void setFlushInterval(int msecs);
    int flushInterval() const;
    
    // 坐标转换
    void setScreenSize(const QSize &size);
    QSize screenSize() const;
    
    void setScaleFactor(double factor);
    double scaleFactor() const;
    
    QPoint transformCoordinates(const QPoint &point) const;
    
    // 统计信息
    quint64 totalEventsProcessed() const;
    quint64 mouseEventsProcessed() const;
    quint64 keyboardEventsProcessed() const;
    double averageProcessingTime() const;
    
signals:
    void inputEventReady(const InputEvent &event);
    void inputEventsReady(const QList<InputEvent> &events);
    void mouseEventProcessed(const QPoint &position, int button, InputEventType type);
    void keyboardEventProcessed(int key, int modifiers, InputEventType type, const QString &text);
    void errorOccurred(const QString &error);
    
public slots:
    void processInputQueue();
    void flushInputBuffer();
    void clearInputQueue();
    
private slots:
    void onFlushTimer();
    void onDelayTimer();
    
private:
    void queueInputEvent(const InputEvent &event);
    void processEvent(const InputEvent &event);
    bool shouldFilterEvent(const InputEvent &event) const;
    
    void updateStatistics(const InputEvent &event, qint64 processingTime);
    
    // 输入状态
    bool m_enabled;
    bool m_mouseEnabled;
    bool m_keyboardEnabled;
    
    // 过滤设置
    bool m_mouseFilter;
    bool m_keyboardFilter;
    QList<int> m_filteredKeys;
    
    // 缓冲和延迟
    QQueue<InputEvent> m_inputQueue;
    QTimer *m_flushTimer;
    QTimer *m_delayTimer;
    int m_inputDelay;
    int m_bufferSize;
    int m_flushInterval;
    
    // 坐标转换
    QSize m_screenSize;
    double m_scaleFactor;
    
    // 统计信息
    quint64 m_totalEvents;
    quint64 m_mouseEvents;
    quint64 m_keyboardEvents;
    QList<qint64> m_processingTimes;
    
    // 线程安全
    QMutex m_mutex;
};

// 输入处理工作线程
class InputWorker : public QObject
{
    Q_OBJECT
    
public:
    explicit InputWorker(QObject *parent = nullptr);
    ~InputWorker();
    
    void setInputHandler(InputHandler *handler);
    
public slots:
    void startWork();
    void stopWork();
    void processInput(const InputEvent &event);
    void processInputBatch(const QList<InputEvent> &events);
    
signals:
    void inputProcessed(const InputEvent &event);
    void batchProcessed(const QList<InputEvent> &events);
    void workFinished();
    
private:
    InputHandler *m_inputHandler;
    bool m_working;
    QMutex m_mutex;
};

#endif // INPUTHANDLER_H