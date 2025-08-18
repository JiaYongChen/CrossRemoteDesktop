#include "constants.h"

// File/frame sizes
const int CoreConstants::DEFAULT_MAX_FILE_SIZE = 10 * 1024 * 1024; // 10MB
const int CoreConstants::MAX_FRAME_SIZE        = 10 * 1024 * 1024; // 10MB
const int CoreConstants::COMPRESSION_THRESHOLD = 1 * 1024 * 1024;  // 1MB

// Buffers
const int CoreConstants::COMPRESSION_BUFFER_SIZE   = 8192;
const int CoreConstants::DECOMPRESSION_BUFFER_SIZE = 8192;
const int CoreConstants::STREAM_BUFFER_SIZE        = 4096;
const int CoreConstants::IMAGE_BUFFER_SIZE         = 16384;

// Capture and frame-rate
const int CoreConstants::DEFAULT_FRAME_RATE   = 60;
const int CoreConstants::MIN_FRAME_RATE       = 1;
const int CoreConstants::MAX_FRAME_RATE       = 120;
const int CoreConstants::DEBUG_LOG_INTERVAL   = 100;
const int CoreConstants::FAILURE_LOG_INTERVAL = 10;
const int CoreConstants::MILLISECONDS_PER_SECOND = 1000;

// Capture quality
constexpr double CoreConstants::DEFAULT_CAPTURE_QUALITY;

// Input processing
const int CoreConstants::DEFAULT_INPUT_BUFFER_SIZE    = 100;
const int CoreConstants::DEFAULT_INPUT_FLUSH_INTERVAL = 10;   // ms
const int CoreConstants::MAX_PROCESSING_TIMES_HISTORY = 1000; // entries

// Input simulator defaults
const int CoreConstants::DEFAULT_MOUSE_SPEED    = 5;    // px/step
const int CoreConstants::DEFAULT_KEYBOARD_DELAY = 10;   // ms
const int CoreConstants::DEFAULT_MOUSE_DELAY    = 10;   // ms
const int CoreConstants::MAX_KEY_VALUE          = 0xFFFF;

// Compression defaults
const int CoreConstants::DEFAULT_ZLIB_LEVEL       = 6;
const int CoreConstants::DEFAULT_ZLIB_WINDOW_BITS = 15;
const int CoreConstants::DEFAULT_ZLIB_MEM_LEVEL   = 8;
const int CoreConstants::MIN_WINDOW_BITS          = 8;
const int CoreConstants::MAX_WINDOW_BITS          = 15;
const int CoreConstants::SMALL_DATA_THRESHOLD     = 1024; // 1KB

// Server
const int CoreConstants::DEFAULT_MAX_CLIENTS    = 1;
const int CoreConstants::CLEANUP_TIMER_INTERVAL = 1000; // ms

// Logging defaults
const int CoreConstants::DEFAULT_MAX_FILE_COUNT     = 5;     // files
const int CoreConstants::DEFAULT_LOG_BUFFER_SIZE    = 1000;  // entries
const int CoreConstants::DEFAULT_LOG_FLUSH_INTERVAL = 5000;  // ms
const int CoreConstants::DEFAULT_ROTATION_INTERVAL  = 24;    // hours
