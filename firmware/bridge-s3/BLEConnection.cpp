#include "BLEConnection.h"
#include <BLE2902.h>

#include "MidiCodec.h"

static bool isMidiStatusByte(uint8_t byte, size_t remaining)
{
    if (byte < 0x80) {
        return false;
    }

    const uint8_t msgLen = MidiCodec::lengthFromStatus(byte);
    return msgLen > 0 && remaining >= msgLen;
}

static bool isLikelyTimestampByte(uint8_t byte, size_t pos, size_t len, const uint8_t* data)
{
    if (!(byte & 0x80) || byte >= 0xF8) {
        return false;
    }

    if (byte > 0xBF) {
        return true;
    }

    if (pos + 1 >= len) {
        return true;
    }

    return isMidiStatusByte(data[pos + 1], len - pos - 1);
}

void BLEConnection::processIncomingBlePacket(const uint8_t* data, size_t length)
{
    size_t i = 0;

    if (i < length && isLikelyTimestampByte(data[i], i, length, data)) {
        i++;
        if (i < length && (data[i] & 0x80) && isLikelyTimestampByte(data[i], i, length, data)) {
            i++;
        }
    }

    while (i < length) {
        while (i < length && isLikelyTimestampByte(data[i], i, length, data)) {
            i++;
        }

        if (i >= length) {
            break;
        }

        const uint8_t status = data[i];
        if (!(status & 0x80)) {
            i++;
            continue;
        }

        const uint8_t msgLen = MidiCodec::lengthFromStatus(status);
        if (msgLen == 0 || i + msgLen > length) {
            break;
        }

        if (midiCallback) {
            midiCallback(data + i, msgLen);
        }
        onMidiDataReceived(data + i, msgLen);
        i += msgLen;
    }
}

BLEConnection::BLEConnection()
    : pServer(nullptr),
      pCharacteristic(nullptr),
      pBleCallback(nullptr),
      sendMutex(nullptr),
      midiCallback(nullptr),
      avgLatencyMs_(0)
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
    public:
        BLEConnection* bleCon;
        ServerCallbacks(BLEConnection* con) : bleCon(con) {}

        void onConnect(BLEServer* pServer) override {
            // Request low latency connection parameters (7.5ms - 15ms)
            // This is critical for BLE MIDI latency.
            BLEDevice::setPower(ESP_PWR_LVL_P9); // Max power
            pServer->updateConnParams(pServer->getConnId(), 6, 12, 0, 400);
        }

        void onDisconnect(BLEServer* pServer) override {
            BLEDevice::startAdvertising();
        }
    };
    pServer->setCallbacks(new ServerCallbacks(this));

    BLEService* pService = pServer->createService(BLE_MIDI_SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        BLE_MIDI_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->addDescriptor(new BLE2902());

    class BLECallback : public BLECharacteristicCallbacks {
    public:
        BLEConnection* bleCon;
        BLECallback(BLEConnection* con) : bleCon(con) {}
        void onWrite(BLECharacteristic* characteristic) override {
            auto rxValue = characteristic->getValue();
            if (rxValue.length() >= 2) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(rxValue.c_str());
                bleCon->processIncomingBlePacket(data, rxValue.length());
            }
        }
    };

    delete pBleCallback;
    pBleCallback = new BLECallback(this);
    pCharacteristic->setCallbacks(pBleCallback);
    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_MIDI_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

bool BLEConnection::sendMidi(const uint8_t* data, size_t length) {
    if (!pCharacteristic || !sendMutex || length == 0) return false;
    if (xSemaphoreTake(sendMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    bool sent = false;
    if (isConnected()) {
        // BLE MIDI requirement: [TimestampHigh, TimestampLow, ...MIDI...]
        uint8_t blePacket[20]; // Max for standard message
        uint16_t timestamp = millis() & 0x1FFF;
        
        blePacket[0] = 0x80 | ((timestamp >> 7) & 0x3F);
        blePacket[1] = 0x80 | (timestamp & 0x7F);
        
        size_t outLen = 2;
        for (size_t i = 0; i < length && outLen < sizeof(blePacket); i++) {
            blePacket[outLen++] = data[i];
        }

        pCharacteristic->setValue(blePacket, outLen);
        pCharacteristic->notify();
        sent = true;
    }

    xSemaphoreGive(sendMutex);
    return sent;
}

void BLEConnection::recordForwardLatency(uint32_t latencyMs)
{
    if (avgLatencyMs_ == 0) {
        avgLatencyMs_ = static_cast<uint16_t>(latencyMs);
    } else {
        avgLatencyMs_ = static_cast<uint16_t>((avgLatencyMs_ * 3 + latencyMs) / 4);
    }
}

void BLEConnection::task() {}

bool BLEConnection::isConnected() const {
    if(pServer)
        return (pServer->getConnectedCount() > 0);
    return false;
}

void BLEConnection::setMidiMessageCallback(MIDIMessageCallback cb) {
    midiCallback = cb;
}

void BLEConnection::onMidiDataReceived(const uint8_t* data, size_t length) {
    if (receiveCallback_) {
        receiveCallback_(data, length);
    }
}
