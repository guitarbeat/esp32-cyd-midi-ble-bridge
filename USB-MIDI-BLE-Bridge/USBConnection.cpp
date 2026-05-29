// Originally based on ESP32_Host_MIDI by Saulo Veríssimo 
// https://github.com/sauloverissimo/ESP32_Host_MIDI 
// Modified by Liam Jones, 2025


#include "USBConnection.h"
#include <string.h>
#include <usb/usb_helpers.h>

namespace {

String utf16DescriptorToAscii(const usb_str_desc_t* desc)
{
    if (desc == nullptr || desc->bLength < 2) {
        return String();
    }

    const uint8_t charCount = (desc->bLength - 2) / 2;
    String out;
    out.reserve(charCount);
    for (uint8_t i = 0; i < charCount; i++) {
        const char c = static_cast<char>(desc->wData[i] & 0xFF);
        if (c == '\0') {
            break;
        }
        out += c;
    }
    return out;
}

}  // namespace

#ifndef DEBUG_USB
#define DEBUG_USB 0
#endif

#if DEBUG_USB
#define USB_LOG(...) Serial.printf(__VA_ARGS__)
#define USB_LOG_LN(msg) Serial.println(msg)
#else
#define USB_LOG(...) ((void)0)
#define USB_LOG_LN(msg) ((void)0)
#endif

void USBConnection::loadDeviceName()
{
    deviceName = "";
    if (deviceHandle == nullptr) {
        return;
    }

    usb_device_info_t devInfo = {};
    if (usb_host_device_info(deviceHandle, &devInfo) != ESP_OK) {
        return;
    }

    deviceName = utf16DescriptorToAscii(devInfo.str_desc_product);
    if (deviceName.length() == 0) {
        deviceName = utf16DescriptorToAscii(devInfo.str_desc_manufacturer);
    }

    if (deviceName.length() > 0) {
        USB_LOG("[USB] Device name: %s\n", deviceName.c_str());
    }
}

USBConnection::USBConnection()
  : isReady(false),
    interval(0),
    lastCheck(0),
    clientHandle(nullptr),
    deviceHandle(nullptr),
    eventFlags(0),
    midiTransfer(nullptr),
    outTransfer(nullptr),
    outTransferBusy(false),
    queueHead(0),
    queueTail(0),
    queueMux(portMUX_INITIALIZER_UNLOCKED),
    firstMidiReceived(false),
    isMidiDeviceConfirmed(false),
    midiInterfaceNumber(-1),
    deviceName(""),
    lastError(""),
    usbTaskHandle(nullptr),
    transferInFlight(false)
{
}

bool USBConnection::begin() {
    usb_host_config_t config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&config);
    if (err != ESP_OK) {
        lastError = "USB host install failed (err=" + String(err) + ")";
        return false;
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = true,
        .max_num_event_msg = 10,
        .async = {
            .client_event_callback = _clientEventCallback,
            .callback_arg = this,
        }
    };
    err = usb_host_client_register(&client_config, &clientHandle);
    if (err != ESP_OK) {
        lastError = "USB client register failed (err=" + String(err) + ")";
        return false;
    }

    // Creates dedicated task on core 0 for continuous USB polling.
    // Ensures MIDI events are never lost due to delays in the main loop.
    // Priority 11 is higher than the MIDI bridge task (10).
    xTaskCreatePinnedToCore(_usbTask, "usb_midi", 4096, this, 11, &usbTaskHandle, 0);

    lastError = "";
    return true;
}

void USBConnection::task() {
    // USB polling runs on the dedicated task (_usbTask on core 0).
    // Here we only drain the queue and forward to onMidiDataReceived().
    processQueue();
}

void USBConnection::onDeviceConnected() {
    // Default implementation (empty). Upper layer can override.
}

void USBConnection::onDeviceDisconnected() {
    // Default implementation (empty).
}

void USBConnection::handleDeviceRemoved()
{
    isReady = false;
    transferInFlight = false;
    outTransferBusy = false;

    if (midiTransfer) {
        usb_host_transfer_free(midiTransfer);
        midiTransfer = nullptr;
    }
    if (outTransfer) {
        usb_host_transfer_free(outTransfer);
        outTransfer = nullptr;
    }

    if (deviceHandle != nullptr) {
        if (midiInterfaceNumber >= 0) {
            usb_host_interface_release(clientHandle, deviceHandle, static_cast<uint8_t>(midiInterfaceNumber));
            midiInterfaceNumber = -1;
        }
        usb_host_device_close(clientHandle, deviceHandle);
        deviceHandle = nullptr;
    }

    portENTER_CRITICAL(&queueMux);
    queueHead = 0;
    queueTail = 0;
    portEXIT_CRITICAL(&queueMux);

    deviceName = "";
    firstMidiReceived = false;
    isMidiDeviceConfirmed = false;
    lastError = "";

    Serial.println("[USB] Keyboard unplugged — plug in again (BLE stays up)");
    onDeviceDisconnected();
}

void USBConnection::onMidiDataReceived(const uint8_t* data, size_t length) {
    // Default implementation (empty). Upper layer should override.
    (void)data;
    (void)length;
}

bool USBConnection::enqueueMidiMessage(const uint8_t* data, size_t /*length*/) {
    portENTER_CRITICAL(&queueMux);
    int next = (queueHead + 1) % QUEUE_SIZE;
    if (next == queueTail) {
        // Queue full; discard the message.
        portEXIT_CRITICAL(&queueMux);
        return false;
    }
    size_t copyLength = 4;
    memcpy(usbQueue[queueHead].data, data, copyLength);
    usbQueue[queueHead].length = copyLength;
    queueHead = next;
    portEXIT_CRITICAL(&queueMux);
    return true;
}

bool USBConnection::dequeueMidiMessage(RawUsbMessage &msg) {
    portENTER_CRITICAL(&queueMux);
    if (queueTail == queueHead) {
        portEXIT_CRITICAL(&queueMux);
        return false;
    }
    msg = usbQueue[queueTail];
    queueTail = (queueTail + 1) % QUEUE_SIZE;
    portEXIT_CRITICAL(&queueMux);
    return true;
}

void USBConnection::processQueue() {
    RawUsbMessage msg;
    while (dequeueMidiMessage(msg)) {
        onMidiDataReceived(msg.data, msg.length);
    }
}

int USBConnection::getQueueSize() const {
    int size = queueHead - queueTail;
    if (size < 0) size += QUEUE_SIZE;
    return size;
}

const RawUsbMessage& USBConnection::getQueueMessage(int index) const {
    int realIndex = (queueTail + index) % QUEUE_SIZE;
    return usbQueue[realIndex];
}

// ---------- USB Task (core 0) ----------

void USBConnection::_usbTask(void* arg) {
    USBConnection* usbCon = static_cast<USBConnection*>(arg);
    for (;;) {
        // Handle library events with a short timeout to maintain responsiveness
        usb_host_lib_handle_events(pdMS_TO_TICKS(1), &usbCon->eventFlags);
        
        // When all devices are gone, free them so new connections can be accepted
        if (usbCon->eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            USB_LOG_LN("[USB] All devices free, ready for reconnect.");
        }
        if (usbCon->eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }

        // Handle client events
        usb_host_client_handle_events(usbCon->clientHandle, pdMS_TO_TICKS(1));

        // We rely on the handle_events timeouts for yielding.
        // If CPU usage is too high, a minimal vTaskDelay(1) can be re-added.
    }
}

// ---------- Internal Callbacks ----------

void USBConnection::_clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg) {
    USB_LOG("[USB] Client event: %d\n", eventMsg->event);
    USBConnection *usbCon = static_cast<USBConnection*>(arg);
    esp_err_t err;
    switch (eventMsg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            Serial.printf("[USB] New device detected at address %d\n", eventMsg->new_dev.address);
            err = usb_host_device_open(usbCon->clientHandle, eventMsg->new_dev.address, &usbCon->deviceHandle);
            if (err != ESP_OK) {
                usbCon->lastError = "Device open failed (err=" + String(err) + ")";
                Serial.println("[USB] " + usbCon->lastError);
                return;
            }
            usbCon->loadDeviceName();
            {
                const usb_config_desc_t *config_desc;
                err = usb_host_get_active_config_descriptor(usbCon->deviceHandle, &config_desc);
                if (err != ESP_OK) {
                    usbCon->lastError = "Config descriptor failed (err=" + String(err) + ")";
                    Serial.println("[USB] " + usbCon->lastError);
                    return;
                }
                Serial.printf("[USB] Active config length: %d\n", config_desc->wTotalLength);
                usbCon->_processConfig(config_desc);
            }
            if (usbCon->isReady) {
                usbCon->lastError = "";
                Serial.printf("[USB] MIDI device '%s' ready.\n", usbCon->deviceName.c_str());
                usbCon->onDeviceConnected();
            } else if (usbCon->lastError.length() == 0) {
                usbCon->lastError = "No MIDI interface found";
                Serial.println("[USB] Error: No MIDI interface found. Check if the device is in 'Generic' USB mode.");
            } else {
                Serial.println("[USB] Error: " + usbCon->lastError);
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            Serial.println("[USB] Device removed.");
            usbCon->handleDeviceRemoved();
            break;
    }
}

void USBConnection::_onReceive(usb_transfer_t *transfer) {
    USBConnection *usbCon = static_cast<USBConnection*>(transfer->context);
    usbCon->transferInFlight = false;

    if (transfer->status == 0 && transfer->actual_num_bytes >= 4) {
        // USB MIDI packets are always 4 bytes. 
        // A single bulk transfer may contain multiple 4-byte packets.
        for (int offset = 0; offset + 4 <= transfer->actual_num_bytes; offset += 4) {
            const uint8_t cin = transfer->data_buffer[offset] & 0x0F;
            const uint8_t status = transfer->data_buffer[offset + 1];
            
            // Filter out Active Sensing (0xFE) to save BLE bandwidth.
            // Roland pianos send this every 300ms.
            if (status == 0xFE) continue;
            
            // CIN 0x0 is "Reserved" or "Invalid" in most MIDI 1.0 cases
            if (cin == 0x00 && status == 0x00) continue;

            if (usbCon->enqueueMidiMessage(transfer->data_buffer + offset, 4)) {
                if (!usbCon->firstMidiReceived) {
                    usbCon->firstMidiReceived = true;
                    Serial.printf("[USB] First MIDI data received (status=0x%02X)\n", status);
                }
            }
        }
    } else if (transfer->status != 0) {
        USB_LOG("[USB] Transfer error: %d\n", transfer->status);
    }

    // Always re-submit the transfer to keep the pipe open
    if (usbCon->isReady) {
        usbCon->transferInFlight = true;
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
            usbCon->transferInFlight = false;
            USB_LOG("[USB] Transfer re-submit failed: %d\n", err);
        }
    }
}

static uint8_t midiStatusToCin(uint8_t status)
{
    if (status >= 0x80 && status <= 0xEF) {
        return status >> 4;
    }

    switch (status) {
        case 0xF0: return 0x04;
        case 0xF1: return 0x02;
        case 0xF2: return 0x03;
        case 0xF3: return 0x02;
        case 0xF6: return 0x05;
        case 0xF7: return 0x05;
        case 0xF8:
        case 0xFA:
        case 0xFB:
        case 0xFC:
        case 0xFE:
        case 0xFF:
            return 0x0F;
        default:
            return 0;
    }
}

bool USBConnection::sendMidiMessage(const uint8_t* data, size_t length)
{
    if (!isReady || outTransfer == nullptr || outTransferBusy || data == nullptr || length == 0) {
        return false;
    }

    const uint8_t cin = midiStatusToCin(data[0]);
    if (cin == 0) {
        return false;
    }

    uint8_t packet[4] = {cin, 0, 0, 0};
    for (size_t i = 0; i < length && i < 3; i++) {
        packet[i + 1] = data[i];
    }

    memcpy(outTransfer->data_buffer, packet, 4);
    outTransfer->num_bytes = 4;
    outTransferBusy = true;

    esp_err_t err = usb_host_transfer_submit(outTransfer);
    if (err != ESP_OK) {
        outTransferBusy = false;
    }

    return err == ESP_OK;
}

void USBConnection::_onSendComplete(usb_transfer_t *transfer)
{
    USBConnection *usbCon = static_cast<USBConnection*>(transfer->context);
    if (usbCon != nullptr) {
        usbCon->outTransferBusy = false;
    }
}

void USBConnection::_processConfig(const usb_config_desc_t *config_desc) {
    const uint8_t* p = config_desc->val;
    uint16_t totalLength = config_desc->wTotalLength;
    uint16_t index = 0;
    bool claimedOk = false;

    Serial.printf("[USB] Scanning %d bytes of descriptors...\n", totalLength);

    while (index < totalLength) {
        if (index + 1 >= totalLength) break;
        uint8_t len = p[index];
        if (len < 2 || (index + len) > totalLength) break;

        uint8_t descriptorType = p[index + 1];
        if (descriptorType == 0x04) { // Interface Descriptor
            if (len < 9) {
                index += len;
                continue;
            }
            uint8_t bInterfaceNumber   = p[index + 2];
            uint8_t bAlternateSetting  = p[index + 3];
            uint8_t bNumEndpoints      = p[index + 4];
            uint8_t bInterfaceClass    = p[index + 5];
            uint8_t bInterfaceSubClass = p[index + 6];
            
            Serial.printf("[USB] Found Interface %d: Class 0x%02X, SubClass 0x%02X, Endpoints %d\n", 
                          bInterfaceNumber, bInterfaceClass, bInterfaceSubClass, bNumEndpoints);

            // MIDI Class is 0x01 (Audio), SubClass 0x03 (MIDI Streaming)
            // Roland/Yamaha Vendor Mode often uses Class 0xFF or Class 0x01 with SubClass 0x00/0xFF
            bool isMidiCandidate = (bInterfaceClass == 0x01 && bInterfaceSubClass == 0x03) || 
                                   (bInterfaceClass == 0x01 && bInterfaceSubClass == 0x00) ||
                                   (bInterfaceClass == 0xFF && bNumEndpoints >= 1);
            
            if (isMidiCandidate) {
                if (bInterfaceClass == 0xFF) {
                    Serial.println("[USB] Warning: Device is in VENDOR mode (Class 0xFF). Roland pianos MUST be in 'Generic' mode for this bridge.");
                }
                
                Serial.printf("[USB] Attempting to claim interface %d...\n", bInterfaceNumber);
                esp_err_t err = usb_host_interface_claim(clientHandle, deviceHandle, bInterfaceNumber, bAlternateSetting);
                if (err == ESP_OK) {
                    midiInterfaceNumber = static_cast<int8_t>(bInterfaceNumber);
                    uint16_t idx2 = index + len;
                    bool inReady = false;
                    while (idx2 < totalLength) {
                        if (idx2 + 1 >= totalLength) break;
                        uint8_t len2 = p[idx2];
                        if (len2 < 2 || (idx2 + len2) > totalLength) break;
                        uint8_t type2 = p[idx2 + 1];
                        if (type2 == 0x04) break; // Next interface
                        
                        if (type2 == 0x05) { // Endpoint Descriptor
                            if (len2 >= 7) {
                                uint8_t bEndpointAddress = p[idx2 + 2];
                                uint8_t bmAttributes = p[idx2 + 3];
                                uint16_t wMaxPacketSize = (p[idx2 + 4] | (p[idx2 + 5] << 8));
                                uint8_t bInterval = p[idx2 + 6];
                                
                                Serial.printf("[USB]   Endpoint 0x%02X: Attr 0x%02X, MaxPacket %d\n", 
                                              bEndpointAddress, bmAttributes, wMaxPacketSize);

                                if (wMaxPacketSize > 512) wMaxPacketSize = 512;
                                if (wMaxPacketSize == 0) wMaxPacketSize = 64;
                                
                                // Bulk IN endpoint
                                if ((bmAttributes & 0x03) == 0x02 && (bEndpointAddress & 0x80)) {
                                    uint32_t timeout = 3000;
                                    esp_err_t e2 = usb_host_transfer_alloc(wMaxPacketSize, timeout, &midiTransfer);
                                    if (e2 == ESP_OK && midiTransfer != nullptr) {
                                        midiTransfer->device_handle = deviceHandle;
                                        midiTransfer->bEndpointAddress = bEndpointAddress;
                                        midiTransfer->callback = _onReceive;
                                        midiTransfer->context = this;
                                        midiTransfer->num_bytes = wMaxPacketSize;
                                        interval = (bInterval == 0) ? 1 : bInterval;
                                        isReady = true;
                                        claimedOk = true;
                                        inReady = true;
                                        transferInFlight = true;
                                        Serial.printf("[USB]   MIDI IN allocated on 0x%02X\n", bEndpointAddress);
                                    } else {
                                        lastError = "MIDI IN alloc failed (err=" + String(e2) + ")";
                                    }
                                } 
                                // Bulk OUT endpoint
                                else if ((bmAttributes & 0x03) == 0x02 && !(bEndpointAddress & 0x80)) {
                                    esp_err_t e2 = usb_host_transfer_alloc(wMaxPacketSize, 3000, &outTransfer);
                                    if (e2 == ESP_OK && outTransfer != nullptr) {
                                        outTransfer->device_handle = deviceHandle;
                                        outTransfer->bEndpointAddress = bEndpointAddress;
                                        outTransfer->callback = _onSendComplete;
                                        outTransfer->context = this;
                                        outTransfer->num_bytes = wMaxPacketSize;
                                        Serial.printf("[USB]   MIDI OUT allocated on 0x%02X\n", bEndpointAddress);
                                    }
                                }
                            }
                        }
                        idx2 += len2;
                    }
                    if (inReady) {
                        vTaskDelay(pdMS_TO_TICKS(200));
                        usb_host_transfer_submit(midiTransfer);
                        return;
                    } else {
                        Serial.println("[USB] Claimed interface but found no valid MIDI endpoints.");
                        usb_host_interface_release(clientHandle, deviceHandle, bInterfaceNumber);
                        midiInterfaceNumber = -1;
                    }
                } else {
                    Serial.printf("[USB] Failed to claim interface %d (err=%d)\n", bInterfaceNumber, err);
                }
            }
        }
        index += len;
    }
    if (!claimedOk && lastError.length() == 0) {
        lastError = "No USB MIDI interface found";
    }
}
