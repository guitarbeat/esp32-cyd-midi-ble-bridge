#include "BLEConnection.h"
#include <BLE2902.h>

BLEConnection::BLEConnection()
    : pServer(nullptr),
      pCharacteristic(nullptr),
      pBleCallback(nullptr),
      sendMutex(nullptr),
      midiCallback(nullptr)
{
}

BLEConnection::~BLEConnection() {
    if (sendMutex) {
        vSemaphoreDelete(sendMutex);
        sendMutex = nullptr;
    }
    delete pBleCallback;
    pBleCallback = nullptr;
}

void BLEConnection::begin(const std::string& deviceName) {
    if (pServer) {
        return;
    }

    sendMutex = xSemaphoreCreateMutex();
    BLEDevice::init(String(deviceName.c_str()));
    pServer = BLEDevice::createServer();
    class ServerCallbacks : public BLEServerCallbacks {
    void onDisconnect(BLEServer* pServer) override {
        BLEDevice::startAdvertising();
        }
    };
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* pService = pServer->createService(BLE_MIDI_SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        BLE_MIDI_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY    
    ); // Added notify

    // Explicit CCCD descriptor keeps notify subscription reliable on iOS/macOS.
    pCharacteristic->addDescriptor(new BLE2902());


    // Create a write callback that extracts the first 4 bytes and forwards them.
    class BLECallback : public BLECharacteristicCallbacks {
    public:
        BLEConnection* bleCon;
        BLECallback(BLEConnection* con) : bleCon(con) {}
        void onWrite(BLECharacteristic* characteristic) override {
            auto rxValue = characteristic->getValue();
            // If at least 4 bytes are available, extract the first 4.
            if(rxValue.length() >= 4) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(rxValue.c_str());
                // Invoke the user-defined callback (if set).
                if(bleCon->midiCallback) {
                    bleCon->midiCallback(data, 4);
                }
                // Also invoke the virtual callback so subclasses can override.
                bleCon->onMidiDataReceived(data, 4);
            }
        }
    };

    // Store the pointer for cleanup in the destructor
    delete pBleCallback;
    pBleCallback = new BLECallback(this);
    pCharacteristic->setCallbacks(pBleCallback);
    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_MIDI_SERVICE_UUID);
    // Keep the 128-bit BLE MIDI UUID in the advertisement and let the name
    // move to scan response if the payload gets tight on newer ESP32 cores.
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

    // New send MIDI function
bool BLEConnection::sendMidi(const uint8_t* data, size_t length) {
    if (!pCharacteristic || !sendMutex || length == 0) return false;
    if (xSemaphoreTake(sendMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    bool sent = false;
    if (isConnected()) {
    pCharacteristic->setValue(const_cast<uint8_t*>(data), length);
    pCharacteristic->notify();
        sent = true;
    }

    xSemaphoreGive(sendMutex);
    return sent;
}

void BLEConnection::task() {
    // BLE generally does not require periodic processing.
}

bool BLEConnection::isConnected() const {
    if(pServer)
        return (pServer->getConnectedCount() > 0);
    return false;
}

void BLEConnection::setMidiMessageCallback(MIDIMessageCallback cb) {
    midiCallback = cb;
}

void BLEConnection::onMidiDataReceived(const uint8_t* data, size_t length) {
    // Default implementation: no-op.
    (void)data;
    (void)length;
}
