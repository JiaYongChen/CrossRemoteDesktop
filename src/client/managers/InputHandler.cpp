#include "InputHandler.h"
#include "../common/core/config/UiConstants.h"
#include <QtWidgets/QApplication>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>

InputHandler::InputHandler(QObject* parent)
    : QObject(parent)
    , m_enabled(true)
    , m_mouseEnabled(true)
    , m_keyboardEnabled(true)
    , m_mouseFilter(false)
    , m_keyboardFilter(false)
    , m_flushTimer(new QTimer(this))
    , m_delayTimer(new QTimer(this))
    , m_inputDelay(0)
    , m_bufferSize(UIConstants::DEFAULT_INPUT_BUFFER_SIZE)
    , m_flushInterval(UIConstants::DEFAULT_INPUT_FLUSH_INTERVAL)
    , m_scaleFactor(1.0)
    , m_totalEvents(0)
    , m_mouseEvents(0)
    , m_keyboardEvents(0) {
    // 设置定时器
    m_flushTimer->setSingleShot(false);
    m_flushTimer->setInterval(m_flushInterval);
    connect(m_flushTimer, &QTimer::timeout, this, &InputHandler::onFlushTimer);

    m_delayTimer->setSingleShot(true);
    connect(m_delayTimer, &QTimer::timeout, this, &InputHandler::onDelayTimer);
}

InputHandler::~InputHandler() {
}

void InputHandler::setEnabled(bool enabled) {
    m_enabled = enabled;
}

bool InputHandler::isEnabled() const {
    return m_enabled;
}

void InputHandler::setMouseEnabled(bool enabled) {
    m_mouseEnabled = enabled;
}

bool InputHandler::isMouseEnabled() const {
    return m_mouseEnabled;
}

void InputHandler::setKeyboardEnabled(bool enabled) {
    m_keyboardEnabled = enabled;
}

bool InputHandler::isKeyboardEnabled() const {
    return m_keyboardEnabled;
}

void InputHandler::handleMouseMove(const QPoint& position) {
    if ( !m_enabled || !m_mouseEnabled ) return;

    InputEvent event;
    event.type = InputEventType::MouseMove;
    event.position = transformCoordinates(position);
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    queueInputEvent(event);
    m_mouseEvents++;
}

void InputHandler::handleMousePress(const QPoint& position, Qt::MouseButton button) {
    if ( !m_enabled || !m_mouseEnabled ) return;

    InputEvent event;
    event.type = InputEventType::MousePress;
    event.position = transformCoordinates(position);
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    queueInputEvent(event);
    m_mouseEvents++;
}

void InputHandler::handleMouseRelease(const QPoint& position, Qt::MouseButton button) {
    if ( !m_enabled || !m_mouseEnabled ) return;

    InputEvent event;
    event.type = InputEventType::MouseRelease;
    event.position = transformCoordinates(position);
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    queueInputEvent(event);
    m_mouseEvents++;
}

void InputHandler::handleMouseWheel(const QPoint& position, int delta) {
    if ( !m_enabled || !m_mouseEnabled ) return;

    InputEvent event;
    event.type = InputEventType::MouseWheel;
    event.position = transformCoordinates(position);
    event.wheelDelta = delta;
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    queueInputEvent(event);
    m_mouseEvents++;
}

void InputHandler::handleKeyPress(int key, Qt::KeyboardModifiers modifiers, const QString& text) {
    if ( !m_enabled || !m_keyboardEnabled ) return;

    InputEvent event;
    event.type = InputEventType::KeyPress;
    event.key = key;
    event.modifiers = static_cast<int>(modifiers);
    event.text = text;
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    queueInputEvent(event);
    m_keyboardEvents++;
}

void InputHandler::handleKeyRelease(int key, Qt::KeyboardModifiers modifiers) {
    if ( !m_enabled || !m_keyboardEnabled ) return;

    InputEvent event;
    event.type = InputEventType::KeyRelease;
    event.key = key;
    event.modifiers = static_cast<int>(modifiers);
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    queueInputEvent(event);
    m_keyboardEvents++;
}

void InputHandler::handleInputEvents(const QList<InputEvent>& events) {
    for ( const auto& event : events ) {
        queueInputEvent(event);
    }
}

void InputHandler::setMouseFilter(bool enable) {
    m_mouseFilter = enable;
}

bool InputHandler::mouseFilter() const {
    return m_mouseFilter;
}

void InputHandler::setKeyboardFilter(bool enable) {
    m_keyboardFilter = enable;
}

bool InputHandler::keyboardFilter() const {
    return m_keyboardFilter;
}

void InputHandler::addFilteredKey(int key) {
    if ( !m_filteredKeys.contains(key) ) {
        m_filteredKeys.append(key);
    }
}

void InputHandler::removeFilteredKey(int key) {
    m_filteredKeys.removeAll(key);
}

void InputHandler::clearFilteredKeys() {
    m_filteredKeys.clear();
}

QList<int> InputHandler::filteredKeys() const {
    return m_filteredKeys;
}

void InputHandler::setInputDelay(int msecs) {
    m_inputDelay = msecs;
}

int InputHandler::inputDelay() const {
    return m_inputDelay;
}

void InputHandler::setBufferSize(int size) {
    m_bufferSize = size;
}

int InputHandler::bufferSize() const {
    return m_bufferSize;
}

void InputHandler::setFlushInterval(int msecs) {
    m_flushInterval = msecs;
    m_flushTimer->setInterval(msecs);
}

int InputHandler::flushInterval() const {
    return m_flushInterval;
}

void InputHandler::setScreenSize(const QSize& size) {
    m_screenSize = size;
}

QSize InputHandler::screenSize() const {
    return m_screenSize;
}

void InputHandler::setScaleFactor(double factor) {
    m_scaleFactor = factor;
}

double InputHandler::scaleFactor() const {
    return m_scaleFactor;
}

QPoint InputHandler::transformCoordinates(const QPoint& point) const {
    return QPoint(static_cast<int>(point.x() * m_scaleFactor),
        static_cast<int>(point.y() * m_scaleFactor));
}

quint64 InputHandler::totalEventsProcessed() const {
    return m_totalEvents;
}

quint64 InputHandler::mouseEventsProcessed() const {
    return m_mouseEvents;
}

quint64 InputHandler::keyboardEventsProcessed() const {
    return m_keyboardEvents;
}

double InputHandler::averageProcessingTime() const {
    if ( m_processingTimes.isEmpty() ) return 0.0;

    qint64 total = 0;
    for ( qint64 time : m_processingTimes ) {
        total += time;
    }
    return static_cast<double>(total) / m_processingTimes.size();
}

void InputHandler::processInputQueue() {
    QMutexLocker locker(&m_mutex);

    while ( !m_inputQueue.isEmpty() ) {
        InputEvent event = m_inputQueue.dequeue();
        processEvent(event);
    }
}

void InputHandler::flushInputBuffer() {
    processInputQueue();
}

void InputHandler::clearInputQueue() {
    QMutexLocker locker(&m_mutex);
    m_inputQueue.clear();
}

void InputHandler::onFlushTimer() {
    flushInputBuffer();
}

void InputHandler::onDelayTimer() {
    // 延迟处理逻辑
}

void InputHandler::queueInputEvent(const InputEvent& event) {
    if ( shouldFilterEvent(event) ) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    if ( m_inputQueue.size() >= m_bufferSize ) {
        m_inputQueue.dequeue(); // 移除最旧的事件
    }

    m_inputQueue.enqueue(event);
    m_totalEvents++;

    emit inputEventReady(event);
}

void InputHandler::processEvent(const InputEvent& event) {
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // 处理事件的具体逻辑
    switch ( event.type ) {
        case InputEventType::MouseMove:
        case InputEventType::MousePress:
        case InputEventType::MouseRelease:
            emit mouseEventProcessed(event.position, event.type);
            break;
        case InputEventType::KeyPress:
        case InputEventType::KeyRelease:
            emit keyboardEventProcessed(event.key, event.modifiers, event.type, event.text);
            break;
        default:
            break;
    }

    qint64 endTime = QDateTime::currentMSecsSinceEpoch();
    qint64 processingTime = endTime - startTime;

    // 更新统计信息
    m_processingTimes.append(processingTime);
    if ( m_processingTimes.size() > UIConstants::MAX_PROCESSING_TIMES_HISTORY ) {
        m_processingTimes.removeFirst();
    }
}

bool InputHandler::shouldFilterEvent(const InputEvent& event) const {
    switch ( event.type ) {
        case InputEventType::MouseMove:
        case InputEventType::MousePress:
        case InputEventType::MouseRelease:
        case InputEventType::MouseWheel:
            return m_mouseFilter;
        case InputEventType::KeyPress:
        case InputEventType::KeyRelease:
            return m_keyboardFilter || m_filteredKeys.contains(event.key);
        default:
            return false;
    }
}

// InputWorker implementation
InputWorker::InputWorker(QObject* parent)
    : QObject(parent)
    , m_inputHandler(nullptr)
    , m_working(false) {
}

InputWorker::~InputWorker() {
}

void InputWorker::setInputHandler(InputHandler* handler) {
    m_inputHandler = handler;
}

void InputWorker::startWork() {
    QMutexLocker locker(&m_mutex);
    m_working = true;
}

void InputWorker::stopWork() {
    QMutexLocker locker(&m_mutex);
    m_working = false;
    emit workFinished();
}

void InputWorker::processInput(const InputEvent& event) {
    if ( !m_working || !m_inputHandler ) return;

    // 处理单个输入事件
    emit inputProcessed(event);
}

void InputWorker::processInputBatch(const QList<InputEvent>& events) {
    if ( !m_working || !m_inputHandler ) return;

    // 处理批量输入事件
    emit batchProcessed(events);
}