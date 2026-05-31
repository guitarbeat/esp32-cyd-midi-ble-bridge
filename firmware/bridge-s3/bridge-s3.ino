#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support."
#endif

#include "Board.h"
#include "InputManager.h"
#include "USBConnection.h"
#include "BLEConnection.h"
#include "ConnectivityManager.h"
#include "BridgeSystem.h"
#include "MidiBridge.h"
#include "BridgeUi.h"
#include "animation/BongoCat.h"

// System Components
static Board* board = createBoard();
static Arduino_Canvas* canvas = nullptr;

static USBConnection usbMidi;
static BLEConnection bleMidi;
static BongoCatDisplay bongoCat;

void setup()
{
    Serial.begin(115200);
    
    // 1. Hardware Bootstrap
    if (!board->begin()) {
        Serial.println("[SYSTEM] Hardware initialization failed.");
        while(1) delay(100);
    }
    
    // 2. System Controller (Brain)
    bridgeSystem.begin();
    
    // 3. UI and Graphics
    canvas = new Arduino_Canvas(240, 240, board->getDisplay());
    if (canvas->begin()) {
        bridgeUi.begin(canvas);
        bridgeUi.setBoard(board);
        bridgeUi.setBongoCat(&bongoCat);
    }
    
    // 4. Input Mapping
    inputManager.mapButton("OK", board->getButtonPin("OK"));
    inputManager.mapButton("UP", board->getButtonPin("UP"));
    inputManager.mapButton("DOWN", board->getButtonPin("DOWN"));
    inputManager.mapButton("MENU", board->getButtonPin("MENU"));

    inputManager.onEvent("OK", [](InputManager::Event e){ 
        if (e == InputManager::Event::kTap) bridgeUi.cycleDisplayMode();
        else if (e == InputManager::Event::kLongHold) bridgeSystem.sendPanic();
    });
    inputManager.onEvent("UP", [](InputManager::Event e){ if (e == InputManager::Event::kTap) bridgeSystem.stepTranspose(1); });
    inputManager.onEvent("DOWN", [](InputManager::Event e){ if (e == InputManager::Event::kTap) bridgeSystem.stepTranspose(-1); });
    inputManager.onEvent("MENU", [](InputManager::Event e){
        if (e == InputManager::Event::kTap) bridgeSystem.cycleMidiChannel();
        else if (e == InputManager::Event::kLongHold) connectivityManager.startProvisioning();
    });

    // 5. MIDI Hub Coordination
    midiBridge.begin(&bridgeSystem.settings(), &bridgeUi);
    midiBridge.setMidiEngine(&bridgeSystem.engine());
    midiBridge.addTransport(&usbMidi);
    midiBridge.addTransport(&bleMidi);
    midiBridge.addTransport(&connectivityManager);

    // 6. Start Transports
    usbMidi.begin();
    bleMidi.begin(bridgeSystem.settings().bleDeviceName());
    connectivityManager.begin();
    bongoCat.begin();

    Serial.println("[SYSTEM] Deep Controller architecture initialized.");
}

void loop()
{
    const uint32_t now = millis();
    
    // Periodic Maintenance
    board->task();
    inputManager.task(now);
    bridgeSystem.tick(now);
    
    usbMidi.task();
    bleMidi.task();
    connectivityManager.task();
    
    // UI Refresh
    bridgeUi.refresh(now);
    canvas->flush();
    
    vTaskDelay(pdMS_TO_TICKS(1));
}
