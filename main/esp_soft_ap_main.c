#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include <stdio.h>
#include <string.h>

#define SSID "ESP32AP"

static const char* TAG = "SOFTAP";
static EventGroupHandle_t wifi_event_group;
const int CLIENT_CONNECTED_BIT = BIT0;
const int CLIENT_DISCONNECTED_BIT = BIT1;


esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch (event->event_id) {
	    case SYSTEM_EVENT_AP_START:
	    	ESP_LOGI("EVENT","AP_START");
	        break;
	    case SYSTEM_EVENT_AP_STACONNECTED:
	    	ESP_LOGI("EVENT","Station connected");
	    	wifi_sta_list_t stationList;
	    	esp_wifi_ap_get_sta_list(&stationList);

			wifi_sta_info_t station = stationList.sta[0];

			ESP_LOGI("EVENT", "MAC: %d:%d:%d:%d:%d:%d", station.mac[0],station.mac[1],station.mac[2],station.mac[3],station.mac[4],station.mac[5]);

			xEventGroupSetBits(wifi_event_group, CLIENT_CONNECTED_BIT);
	        break;
	    case SYSTEM_EVENT_AP_STADISCONNECTED:
	    	ESP_LOGI("EVENT","Station disconnected");
	    	xEventGroupSetBits(wifi_event_group, CLIENT_DISCONNECTED_BIT);
	    	break;
	    case SYSTEM_EVENT_AP_STOP:
			ESP_LOGI("EVENT","AP_STOP");
			break;
	    default:
	        break;
	    }
    return ESP_OK;
}

static void start_dhcp_server(){

    	// initialize the tcp stack
	    tcpip_adapter_init();
        // stop DHCP server
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
        // assign a static IP to the network interface
        tcpip_adapter_ip_info_t info;
        memset(&info, 0, sizeof(info));
        IP4_ADDR(&info.ip, 192, 168, 1, 1);
        IP4_ADDR(&info.gw, 192, 168, 1, 1);//ESP acts as router, so gw addr will be its own addr
        IP4_ADDR(&info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
        // start the DHCP server
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
        printf("DHCP server started \n");
}

void printStationList()
{
	printf(" Connected stations:\n");
	printf("--------------------------------------------------\n");

	wifi_sta_list_t wifi_sta_list;
	tcpip_adapter_sta_list_t adapter_sta_list;

	memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
	memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

	ESP_ERROR_CHECK(esp_wifi_ap_get_sta_list(&wifi_sta_list));
	ESP_ERROR_CHECK(tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list));

	for(int i = 0; i < adapter_sta_list.num; i++) {

		tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
         printf("%d - mac: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x - IP: %s\n", i + 1,
				station.mac[0], station.mac[1], station.mac[2],
				station.mac[3], station.mac[4], station.mac[5],
				ip4addr_ntoa(&(station.ip)));
	}

	printf("\n");
}

static void wifi_init(void)
{

	nvs_flash_init();
	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
	wifi_config_t ap_config = {
		.ap = {
			.ssid = SSID,
			.channel = 0,
			.authmode = WIFI_AUTH_OPEN,
			.ssid_hidden = 0,
			.max_connection = 1,
			.beacon_interval = 100
			
		}
	};
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK( esp_wifi_start() );
    printf("ESP WiFi started in AP mode \n");
}

void print_sta_info(void *pvParam){
    printf("print_sta_info task started \n");
    while(1) {
		EventBits_t staBits = xEventGroupWaitBits(wifi_event_group, CLIENT_CONNECTED_BIT | CLIENT_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
		if((staBits & CLIENT_CONNECTED_BIT) != 0) printf("New station connected\n\n");
        else printf("A station disconnected\n\n");
        printStationList();
	}
}

void app_main(void)
{
	 ESP_LOGI(TAG, "[APP] Startup..");
	 ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	 ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	 start_dhcp_server();
	 wifi_init();
	 xTaskCreate(&print_sta_info,"print_sta_info",4096,NULL,5,NULL);

}
