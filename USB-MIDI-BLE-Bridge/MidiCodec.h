#ifndef MIDI_CODEC_H
#define MIDI_CODEC_H

#include <Arduino.h>
#include <stddef.h>

namespace MidiCodec {

inline uint8_t lengthFromStatus(uint8_t status)
{
    if (status >= 0xF8) {
        return 1;
    }

    switch (status & 0xF0) {
        case 0xC0:
        case 0xD0:
            return 2;
        case 0x80:
        case 0x90:
        case 0xA0:
        case 0xB0:
        case 0xE0:
            return 3;
        default:
            break;
    }

    switch (status) {
        case 0xF1:
        case 0xF3:
            return 2;
        case 0xF2:
            return 3;
        case 0xF6:
        case 0xF7:
            return 1;
        default:
            return 0;
    }
}

inline uint8_t lengthFromUsbCin(uint8_t cin, uint8_t status)
{
    switch (cin & 0x0F) {
        case 0x2:
            return 2;
        case 0x3:
            return 3;
        case 0x4:
            return 3;
        case 0x5:
            return 1;
        case 0x6:
            return 2;
        case 0x7:
            return 3;
        case 0x8:
        case 0x9:
        case 0xA:
        case 0xB:
        case 0xE:
            return 3;
        case 0xC:
        case 0xD:
            return 2;
        case 0xF:
            return 1;
        default:
            return lengthFromStatus(status);
    }
}

inline void appendBleTimestamp(uint8_t* packet, size_t* length, uint16_t timestampMs)
{
    const uint16_t timestamp = timestampMs & 0x1FFF;
    packet[0] = 0x80 | ((timestamp >> 7) & 0x3F);
    packet[1] = 0x80 | (timestamp & 0x7F);
    *length = 2;
}

inline bool buildBlePacket(const uint8_t* usbMidiPacket,
                           size_t usbLength,
                           uint16_t timestampMs,
                           uint8_t* blePacket,
                           size_t* bleLength)
{
    if (usbLength < 4 || blePacket == nullptr || bleLength == nullptr) {
        return false;
    }

    const uint8_t cin = usbMidiPacket[0] & 0x0F;
    const uint8_t status = usbMidiPacket[1];
    const uint8_t midiLength = lengthFromUsbCin(cin, status);

    if (midiLength == 0 || midiLength > 3 || status == 0x00) {
        return false;
    }

    if (status == 0xFE) {
        return false;
    }

    appendBleTimestamp(blePacket, bleLength, timestampMs);

    for (uint8_t i = 0; i < midiLength; i++) {
        blePacket[2 + i] = usbMidiPacket[1 + i];
    }

    *bleLength = 2 + midiLength;
    return true;
}

inline const char* statusName(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80:
            return "NoteOff";
        case 0x90:
            return "NoteOn";
        case 0xA0:
            return "PolyPressure";
        case 0xB0:
            return "ControlChange";
        case 0xC0:
            return "ProgramChange";
        case 0xD0:
            return "ChannelPressure";
        case 0xE0:
            return "PitchBend";
        default:
            return "System";
    }
}

inline const char* noteName(uint8_t note, char* buffer, size_t bufferLength)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int octave = (note / 12) - 1;
    snprintf(buffer, bufferLength, "%s%d", names[note % 12], octave);
    return buffer;
}

}  // namespace MidiCodec

#endif
