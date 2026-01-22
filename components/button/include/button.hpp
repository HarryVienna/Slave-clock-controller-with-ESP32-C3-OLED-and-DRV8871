/**
 * @file button.hpp
 * @brief Button handler class for ESP-IDF with debouncing and press detection
 *
 * This class provides robust button handling with:
 * - Debouncing to filter electrical noise
 * - Short press detection
 * - Double-click detection
 * - Long press detection with optional repeat
 * - Non-blocking operation using FreeRTOS tasks
 */

#ifndef BUTTON_HPP
#define BUTTON_HPP

#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Default timing constants (can be overridden in constructor)
#define BUTTON_DEFAULT_DEBOUNCE_MS      50
#define BUTTON_DEFAULT_LONG_PRESS_MS    500
#define BUTTON_DEFAULT_REPEAT_MS        200
#define BUTTON_DEFAULT_DOUBLE_CLICK_MS  400

// Task configuration
#define BUTTON_TASK_STACK_SIZE          4096
#define BUTTON_TASK_PRIORITY            1

class Button {
public:
    // Press types
    enum class PressType {
        Short,
        Long,
        Double
    };

    // Callback type using std::function for flexibility (lambdas, member functions, etc.)
    using Callback = std::function<void(PressType)>;

    /**
     * @brief Construct a new Button object
     *
     * @param pin GPIO pin number
     * @param active_low true if button connects to GND (default), false if to VCC
     * @param debounce_ms Debounce time in milliseconds
     * @param long_press_ms Threshold for long press detection
     * @param repeat_ms Repeat interval for held button
     * @param double_click_ms Maximum time between clicks for double-click
     */
    Button(gpio_num_t pin,
           bool active_low = true,
           uint32_t debounce_ms = BUTTON_DEFAULT_DEBOUNCE_MS,
           uint32_t long_press_ms = BUTTON_DEFAULT_LONG_PRESS_MS,
           uint32_t repeat_ms = BUTTON_DEFAULT_REPEAT_MS,
           uint32_t double_click_ms = BUTTON_DEFAULT_DOUBLE_CLICK_MS);

    /**
     * @brief Destroy the Button object
     *
     * Stops the monitoring task and releases resources
     */
    ~Button();

    // Prevent copying (each Button owns a FreeRTOS task)
    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;

    // --- Callback setters ---

    void onShortPress(Callback cb)  { _short_press_cb = cb; }
    void onLongPress(Callback cb)   { _long_press_cb = cb; }
    void onDoubleClick(Callback cb) { _double_click_cb = cb; }

    /**
     * @brief Enable or disable repeat for long press
     *
     * When enabled, the long press callback will be called repeatedly
     * while the button is held down
     */
    void setRepeatEnabled(bool enable) { _enable_repeat = enable; }

    /**
     * @brief Wait (blocking) until any button press is detected
     *
     * @return PressType Type of press detected
     */
    PressType waitForPress();

    /**
     * @brief Get current button state
     *
     * @return true if button is currently pressed
     */
    bool isPressed() const;

private:
    // Static task wrapper for FreeRTOS
    static void _taskWrapper(void* param);

    // Main task loop
    void _taskLoop();

    // Read button state (accounts for active_low)
    bool _readState() const;

    // Callback invocation helpers
    void _callShortPress();
    void _callLongPress();
    void _callDoubleClick();

    // GPIO and configuration
    gpio_num_t _pin;
    bool _active_low;
    bool _enable_repeat = false;

    // Timing configuration
    uint32_t _debounce_ms;
    uint32_t _long_press_ms;
    uint32_t _repeat_ms;
    uint32_t _double_click_ms;

    // FreeRTOS task handle
    TaskHandle_t _task_handle = nullptr;

    // Callbacks
    Callback _short_press_cb;
    Callback _long_press_cb;
    Callback _double_click_cb;

    // State variables (volatile for task communication)
    volatile bool _click_detected = false;
    volatile PressType _last_press_type = PressType::Short;

    // Debounce and timing state
    int _last_state = 0;
    uint32_t _last_debounce_time = 0;
    uint32_t _press_start_time = 0;
    uint32_t _last_repeat_time = 0;
    bool _long_press_triggered = false;

    // Double-click state
    uint32_t _last_release_time = 0;
    bool _waiting_for_double_click = false;
};

#endif // BUTTON_HPP
