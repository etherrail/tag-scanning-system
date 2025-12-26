#include "esp_netif_ip_addr.h"

#include <cstring>
#include <string>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"
	#include "esp_event.h"
	#include "esp_log.h"
	#include "esp_netif.h"
	#include "esp_eth.h"
	#include "esp_system.h"
	#include "esp_check.h"
}

class Network {
	public:
		esp_ip4_addr_t address;
		bool ready = false;

		void begin();
};

// shared instance declaration
extern Network network;

#define TAG "NETWORK"

// address assignment callback
static void onAddressAssign(
	void *arg,
	esp_event_base_t event_base,
	int32_t event_id,
	void *event_data
) {
	ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
	network.address = event->ip_info.ip;
	network.ready = true;

	ESP_LOGI(TAG, "address assigned: " IPSTR, IP2STR(&network.address));

	/*
    xTaskCreate(
        [](void *param) {
            const char *server_ip = "192.168.31.137";  // <-- change to your server
            const uint16_t server_port = 49234;        // <-- change to your server port

            ESP_LOGI(TAG, "Connecting to %s:%u", server_ip, server_port);

            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock < 0) {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                vTaskDelete(nullptr);
                return;
            }

            struct sockaddr_in dest_addr = {};
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(server_port);
            inet_pton(AF_INET, server_ip, &dest_addr.sin_addr.s_addr);

            int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err != 0) {
                ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
                close(sock);
                vTaskDelete(nullptr);
                return;
            }

            ESP_LOGI(TAG, "Successfully connected");

            const char *request = "Hello from ESP32-P4 over Ethernet!\n";
            err = send(sock, request, strlen(request), 0);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                close(sock);
                vTaskDelete(nullptr);
                return;
            }

            // Optional: receive response
            char rx_buffer[128];
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
            } else if (len == 0) {
                ESP_LOGI(TAG, "Connection closed");
            } else {
                rx_buffer[len] = 0; // Null-terminate
                ESP_LOGI(TAG, "Received %d bytes: '%s'", len, rx_buffer);
            }

            ESP_LOGI(TAG, "Shutting down socket");
            shutdown(sock, 0);
            close(sock);
            vTaskDelete(nullptr);
        },
        "tcp_client_task", 4096, nullptr, 5, nullptr);
	*/
}

// shared instance definition
Network network;

void Network::begin() {
	// create network interface
	ESP_ERROR_CHECK(esp_netif_init());

	// create event loop
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// register ip callback
	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		IP_EVENT,
		IP_EVENT_ETH_GOT_IP,
		&onAddressAssign,
		nullptr,
		nullptr
	));

	// create interface
	esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
	esp_netif_t *eth_netif = esp_netif_new(&cfg);
	ESP_ERROR_CHECK(esp_netif_set_default_netif(eth_netif));

	eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
	eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
	eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

	esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
	esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);

	esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
	esp_eth_handle_t eth_handle = nullptr;
	ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

	// attach driver
	ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
	ESP_ERROR_CHECK(esp_eth_start(eth_handle));

	ESP_LOGI(TAG, "waiting for DHCP");

	while (!network.ready) {
		vTaskDelay(1);
	}

	ESP_LOGI(TAG, "ready");
}
