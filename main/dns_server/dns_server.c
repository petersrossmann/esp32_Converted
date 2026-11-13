/*
 * dns_server.c — Simple DNS server for captive portal.
 *
 * Listens on UDP port 53 and answers every DNS query with the AP IP address,
 * so devices connecting to the WiFi AP are redirected to the ESP32's web
 * server regardless of what domain they try to resolve.
 */
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dns_server.h"

static const char *TAG = "dns_server";

static void dns_server_task(void *pvParameters)
{
    uint32_t ap_ip = (uint32_t)(uintptr_t)pvParameters;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind to port 53");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port 53 (captive portal -> %u.%u.%u.%u)",
             (unsigned int)((ap_ip >> 24) & 0xFF), (unsigned int)((ap_ip >> 16) & 0xFF),
             (unsigned int)((ap_ip >> 8) & 0xFF), (unsigned int)(ap_ip & 0xFF));

    uint8_t buf[512];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) {
            continue;
        }

        /* Build response: copy the incoming query, then patch header + answer */
        uint8_t resp[512];
        memcpy(resp, buf, (size_t)len);

        /* DNS header flags: QR=1, AA=1, RD=0, RA=0, RCODE=0 */
        resp[2] = 0x81;
        resp[3] = 0x80;
        /* ANCOUNT = 1 */
        resp[6] = 0x00;
        resp[7] = 0x01;
        /* NSCOUNT = 0, ARCOUNT = 0 */
        resp[8] = 0x00;
        resp[9] = 0x00;

        /* Find end of the question section in the copied query */
        int q_offset = 12;  /* after the 12-byte header */
        while (q_offset < len && buf[q_offset] != 0) {
            q_offset += buf[q_offset] + 1;
        }
        q_offset += 1;  /* null-terminating label */
        q_offset += 4;  /* qtype (2) + qclass (2) */

        int offset = q_offset;

        /* Answer: name pointer to the question (0xC00C) */
        resp[offset++] = 0xC0;
        resp[offset++] = 0x0C;
        /* Type A */
        resp[offset++] = 0x00;
        resp[offset++] = 0x01;
        /* Class IN */
        resp[offset++] = 0x00;
        resp[offset++] = 0x01;
        /* TTL = 60 seconds */
        resp[offset++] = 0x00;
        resp[offset++] = 0x00;
        resp[offset++] = 0x00;
        resp[offset++] = 0x3C;
        /* RDLENGTH = 4 */
        resp[offset++] = 0x00;
        resp[offset++] = 0x04;
        /* RDATA = AP IP address */
        resp[offset++] = (uint8_t)((ap_ip >> 24) & 0xFF);
        resp[offset++] = (uint8_t)((ap_ip >> 16) & 0xFF);
        resp[offset++] = (uint8_t)((ap_ip >> 8) & 0xFF);
        resp[offset++] = (uint8_t)(ap_ip & 0xFF);

        sendto(sock, (const char *)resp, offset, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

void dns_server_start(uint32_t ap_ip)
{
    xTaskCreate(dns_server_task, "dns_server", 2048,
                (void *)(uintptr_t)ap_ip, 5, NULL);
}
