#include "../src/ble_line_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string>

static void append(BleLineQueue& queue, const std::string& text, uint32_t now = 100) {
    queue.append(reinterpret_cast<const uint8_t*>(text.data()), text.size(), now);
}
static std::string pop(BleLineQueue& queue) {
    char command[BleLineQueue::MAX_LINE + 1] = {};
    return queue.pop(command, sizeof(command)) ? command : "";
}
int main() {
    BleLineQueue queue;
    append(queue, "screen po");
    assert(pop(queue).empty());
    append(queue, "modoro\npet feed\r\n\n");
    assert(pop(queue) == "screen pomodoro");
    assert(pop(queue) == "pet feed\r");
    assert(pop(queue).empty());
    append(queue, "st\ratus\npet\tfeed\n");
    assert(pop(queue) == "st\ratus"); // The parser must see, and reject, embedded CR.
    assert(pop(queue) == "pet\tfeed");

    append(queue, std::string(95, 'x') + "\n");
    assert(pop(queue).size() == 95);
    append(queue, std::string(96, 'x') + "pet feed\nstatus\n");
    assert(pop(queue) == "status");
    assert(pop(queue).empty());
    const uint32_t rejected = queue.rejected();
    append(queue, std::string("screen ") + '\0' + "pet\nstatus\n");
    assert(pop(queue) == "status");
    append(queue, "pet\x7f feed\nstatus\n");
    assert(pop(queue) == "status");
    append(queue, "pet \xc3\xa9\nstatus\n");
    assert(pop(queue) == "status");
    assert(queue.rejected() == rejected + 3);

    for (int i = 0; i < 12; ++i) append(queue, "bright " + std::to_string(i) + "\n");
    assert(queue.count() == 8);
    for (int i = 0; i < 8; ++i) assert(pop(queue) == "bright " + std::to_string(i));
    assert(pop(queue).empty());
    append(queue, "screen pet\n");
    char tooSmall[4];
    assert(!queue.pop(tooSmall, sizeof(tooSmall)));
    assert(pop(queue) == "screen pet");

    append(queue, "screen pe", 0xfffffff0);
    queue.expire(uint32_t(0xfffffff0 + uint32_t(5000)));
    append(queue, "t\nstatus\n", 6000);
    assert(pop(queue) == "status");
    assert(pop(queue).empty());

    append(queue, "pet feed\npartial");
    queue.reset();
    assert(queue.count() == 0);
    append(queue, "status\n");
    assert(pop(queue) == "status");
    // Repeat fragmented input and ring-buffer wrap to expose indexing mistakes.
    for (int i = 0; i < 10000; ++i) {
        append(queue, "p"); append(queue, "et "); append(queue, "play\n");
        assert(pop(queue) == "pet play");
    }
    puts("BLE line queue: fragments, boundaries, invalid bytes, overflow, timeout/wrap, session reset PASS");
}
