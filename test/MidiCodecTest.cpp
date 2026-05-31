#include <iostream>
#include <cassert>
#include <cstring>
#include "../firmware/bridge-s3/MidiCodec.h"

void testLengthFromStatus() {
    assert(MidiCodec::lengthFromStatus(0x90) == 3); // Note On
    assert(MidiCodec::lengthFromStatus(0x80) == 3); // Note Off
    assert(MidiCodec::lengthFromStatus(0xC0) == 2); // Program Change
    assert(MidiCodec::lengthFromStatus(0xF8) == 1); // Timing Clock
    std::cout << "testLengthFromStatus passed!" << std::endl;
}

void testNoteName() {
    char buffer[12];
    MidiCodec::noteName(60, buffer, sizeof(buffer)); // Middle C
    assert(std::string(buffer) == "C4");
    
    MidiCodec::noteName(21, buffer, sizeof(buffer)); // A0
    assert(std::string(buffer) == "A0");
    
    MidiCodec::noteName(108, buffer, sizeof(buffer)); // C8
    assert(std::string(buffer) == "C8");
    std::cout << "testNoteName passed!" << std::endl;
}

void testParser() {
    static uint8_t lastStatus = 0;
    static uint8_t lastData[3] = {0};
    static size_t lastLen = 0;

    auto callback = [](uint8_t status, const uint8_t* data, size_t length, void* arg) {
        lastStatus = status;
        lastLen = length;
        if (length > 0 && data) std::memcpy(lastData, data, length);
    };

    MidiCodec::Parser parser(callback);

    // 1. Simple Note On
    parser.parse(0x90);
    parser.parse(60);
    parser.parse(100);
    assert(lastStatus == 0x90);
    assert(lastLen == 2);
    assert(lastData[0] == 60);
    assert(lastData[1] == 100);

    // 2. Running Status
    lastStatus = 0;
    parser.parse(61);
    parser.parse(101);
    assert(lastStatus == 0x90);
    assert(lastData[0] == 61);
    assert(lastData[1] == 101);

    // 3. Real-time message during Note On (interleaved)
    lastStatus = 0;
    parser.parse(62);
    parser.parse(0xF8); // Timing Clock
    assert(lastStatus == 0xF8);
    assert(lastLen == 0);
    parser.parse(102);
    assert(lastStatus == 0x90);
    assert(lastData[0] == 62);
    assert(lastData[1] == 102);

    // 4. SysEx
    lastStatus = 0;
    parser.parse(0xF0);
    assert(lastStatus == 0xF0);
    assert(lastLen == 0);
    parser.parse(0x01);
    assert(lastStatus == 0xF0);
    assert(lastLen == 1);
    assert(lastData[0] == 0x01);
    parser.parse(0xF7);
    assert(lastStatus == 0xF7);
    assert(lastLen == 0);

    std::cout << "testParser passed!" << std::endl;
}

int main() {
    testLengthFromStatus();
    testNoteName();
    testParser();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}

