#include "dns_server.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

// Captive DNS: every A query resolves to the AP so any hostname opens the editor.
// Other record types get an empty NOERROR answer instead of a mismatched A record.
static void dns_task(void *)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) vTaskDelete(nullptr);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(sock);
        vTaskDelete(nullptr);
    }
    uint8_t packet[512];
    for (;;) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        if (len < 12) continue;
        if (packet[2] & 0x80) continue;                 // not a query
        if (packet[4] != 0 || packet[5] != 1) continue; // exactly one question

        int pos = 12;
        bool valid = false;
        while (pos < len) {
            uint8_t label = packet[pos];
            if (label == 0) {
                valid = true;
                break;
            }
            if (label & 0xC0) break;
            pos += label + 1;
        }
        if (!valid || pos + 5 > len) continue;
        const uint16_t qtype = (uint16_t)(packet[pos + 1] << 8 | packet[pos + 2]);
        pos += 5; // zero label + QTYPE + QCLASS

        const bool answer = qtype == 1; // A
        if (answer && pos + 16 > (int)sizeof(packet)) continue;
        packet[2] = 0x84 | (packet[2] & 0x01); // response, authoritative, keep RD
        packet[3] = 0x80;                      // recursion available, NOERROR
        packet[6] = 0;
        packet[7] = answer ? 1 : 0;
        packet[8] = packet[9] = packet[10] = packet[11] = 0;
        if (answer) {
            const uint8_t record[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1};
            memcpy(packet + pos, record, sizeof(record));
            pos += sizeof(record);
        }
        sendto(sock, packet, pos, 0, reinterpret_cast<sockaddr *>(&from), from_len);
    }
}

void dns_server_start() { xTaskCreate(dns_task, "dns_server", 3072, nullptr, 3, nullptr); }
