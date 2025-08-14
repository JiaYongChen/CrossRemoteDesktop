#ifndef CORE_CONSTANTS_H
#define CORE_CONSTANTS_H

// Core (non-UI) constants used by server, protocol, compression, etc.
// Keep UI-related (colors, window sizes) in uiconstants.h.

class CoreConstants {
public:
    // File/frame sizes
    static const int DEFAULT_MAX_FILE_SIZE; // 10MB
    static const int MAX_FRAME_SIZE;        // 10MB
    static const int COMPRESSION_THRESHOLD; // 1MB

    // Buffers
    static const int COMPRESSION_BUFFER_SIZE;
    static const int DECOMPRESSION_BUFFER_SIZE;
    static const int STREAM_BUFFER_SIZE;
    static const int IMAGE_BUFFER_SIZE;

    // Capture and frame-rate
    static const int DEFAULT_FRAME_RATE;
    static const int MIN_FRAME_RATE;
    static const int MAX_FRAME_RATE;
    static const int DEBUG_LOG_INTERVAL;
    static const int FAILURE_LOG_INTERVAL;
    static const int MILLISECONDS_PER_SECOND;

    // Capture quality
    static constexpr double DEFAULT_CAPTURE_QUALITY = 0.9;

    // Input processing
    static const int DEFAULT_INPUT_BUFFER_SIZE;
    static const int DEFAULT_INPUT_FLUSH_INTERVAL;   // ms
    static const int MAX_PROCESSING_TIMES_HISTORY;   // entries

    // Input simulator defaults (moved from UI constants)
    static const int DEFAULT_MOUSE_SPEED;      // px/step
    static const int DEFAULT_KEYBOARD_DELAY;   // ms
    static const int DEFAULT_MOUSE_DELAY;      // ms
    static const int MAX_KEY_VALUE;            // max key code value

    // Compression defaults
    static const int DEFAULT_ZLIB_LEVEL;
    static const int DEFAULT_ZLIB_WINDOW_BITS;
    static const int DEFAULT_ZLIB_MEM_LEVEL;
    static const int MIN_WINDOW_BITS;
    static const int MAX_WINDOW_BITS;
    static const int DEFAULT_LZ4_LEVEL;
    static const int DEFAULT_ZSTD_LEVEL;
    static const int SMALL_DATA_THRESHOLD; // 1KB

    // Server
    static const int DEFAULT_MAX_CLIENTS;
    static const int CLEANUP_TIMER_INTERVAL; // ms

private:
    CoreConstants() = delete;
};

#endif // CORE_CONSTANTS_H
