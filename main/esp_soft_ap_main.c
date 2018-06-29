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
	        esp_wifi_connect();
	        break;
	    case SYSTEM_EVENT_STA_GOT_IP:
	        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
	                if (!g_station_list) {
	            g_station_list = malloc(sizeof(station_info_t));
	            g_station_list->next = NULL;
	            ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
	            ESP_ERROR_CHECK(esp_wifi_set_promiscuous(1));
	        }
	        break;
	    case SYSTEM_EVENT_STA_DISCONNECTED:
	        esp_wifi_connect();
	        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
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

//    tcpip_adapter_init();
//    wifi_event_group = xEventGroupCreate();
//    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
//    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
//    wifi_config_t wifi_config = {
//        .sta = {
//            .ssid = CONFIG_WIFI_SSID,
//            .password = CONFIG_WIFI_PASSWORD,
//        },
//    };
//    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
//    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", CONFIG_WIFI_SSID, "******");
//    ESP_ERROR_CHECK(esp_wifi_start());
//
//    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}


void app_main(void)
{

//    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
//    int level = 0;
//    while (true) {
//        gpio_set_level(GPIO_NUM_4, level);
//        level = !level;
//        printf(cfg.event_handler);
//           if (cfg.event_handler == SYSTEM_EVENT_AP_STACONNECTED  ) {
//           	printf("AP station Connected");
//           }
//        vTaskDelay(300 / portTICK_PERIOD_MS);
//    }
}

