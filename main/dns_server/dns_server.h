#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start a simple DNS server that answers every query with the AP IP address,
 * enabling a captive portal for clients connecting to the WiFi AP.
 *
 * @param ap_ip  AP IP address in host byte order (from esp_netif_ip_info_t.ip.addr)
 */
void dns_server_start(uint32_t ap_ip);

#ifdef __cplusplus
}
#endif
#endif /* DNS_SERVER_H */