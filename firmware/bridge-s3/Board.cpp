#include "Board.h"

/**
 * @brief Implementation for the official Espressif ESP32-S3-USB-OTG board.
 */
class S3UsbOtgBoard : public Board {
public:
    S3UsbOtgBoard() : 
        bus(new Arduino_ESP32LCD8(
            45 /* DC */, 0 /* CS */, 47 /* WR */, 21 /* RD */,
            39 /* D0 */, 40 /* D1 */, 41 /* D2 */, 42 /* D3 */,
            45 /* D4 */, 46 /* D5 */, 47 /* D6 */, 48 /* D7 */)),
        display(new Arduino_ST7789(
            bus, -1 /* RST */, 0 /* rotation */, true /* IPS */,
            240 /* width */, 240 /* height */,
            0 /* col_offset1 */, 0 /* row_offset1 */,
            0 /* col_offset2 */, 0 /* row_offset2 */))
    {}

    bool begin() override {
        // 1. USB Host Power Pins
        pinMode(18 /* SEL */, OUTPUT); digitalWrite(18, HIGH);
        pinMode(12 /* VBUS */, OUTPUT); digitalWrite(12, HIGH);
        pinMode(17 /* LIMIT */, OUTPUT); digitalWrite(17, HIGH);
        pinMode(13 /* BOOST */, OUTPUT); digitalWrite(13, LOW);

        // 2. LCD Initialization
        if (!display->begin(80000000)) {
            return false;
        }

        // 3. Backlight Setup
        pinMode(14 /* Backlight */, OUTPUT);
        setBacklight(255);

        // 4. Buttons (Pull-ups)
        pinMode(0 /* OK/Boot */, INPUT_PULLUP);
        pinMode(10 /* UP */, INPUT_PULLUP);
        pinMode(11 /* DOWN */, INPUT_PULLUP);
        // Note: GPIO 14 is shared with backlight on this board.
        // It's used as an input in some contexts but we'll stick to backlight output for now
        // or handle it in InputManager.

        return true;
    }

    Arduino_GFX* getDisplay() override {
        return display;
    }

    void setBacklight(uint8_t level) override {
        // Simple digital toggle for now as shared with button 14
        digitalWrite(14, level > 0 ? HIGH : LOW);
    }

    float getBatteryVoltage() override {
        const int raw = analogRead(1 /* BATT_SENSE */);
        // Voltage divider calculation (S3-USB-OTG has a 1/2 divider)
        return (raw / 4095.0f) * 3.3f * 2.0f;
    }

    bool isUsbPowered() override {
        return getBatteryVoltage() > 4.4f;
    }

    int getButtonPin(const char* name) override {
        if (strcmp(name, "OK") == 0) return 0;
        if (strcmp(name, "UP") == 0) return 10;
        if (strcmp(name, "DOWN") == 0) return 11;
        if (strcmp(name, "MENU") == 0) return 14;
        return -1;
    }

private:
    Arduino_DataBus* bus;
    Arduino_GFX* display;
};

Board* createBoard() {
    return new S3UsbOtgBoard();
}
