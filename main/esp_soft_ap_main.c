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

#define SSID "ESP32AP"

static const char* TAG = "SOFTAP";
static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;


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

	    	xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
	        break;
	    case SYSTEM_EVENT_AP_STADISCONNECTED:
	    	ESP_LOGI("EVENT","Station disconnected");
	    	break;
	    case SYSTEM_EVENT_AP_PROBEREQRECVED:
	    	ESP_LOGI("EVENT","AP_PROBEREQRECVED");
	    	break;
	    case SYSTEM_EVENT_AP_STOP:
			ESP_LOGI("EVENT","AP_STOP");
			break;
	    default:
	        break;
	    }
    return ESP_OK;
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
	ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_config) );
	ESP_ERROR_CHECK( esp_wifi_start() );
	ESP_LOGI(TAG, "Waiting for wifi");
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

}

void app_main(void)
{
	 ESP_LOGI(TAG, "[APP] Startup..");
	 ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	 ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	wifi_init();

}
