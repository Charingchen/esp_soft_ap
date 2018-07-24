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

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <stdio.h>
#include <string.h>

#define SSID "ESP32AP"
//#define customIp ((u32_t)0xC0A80103UL) //102.168.1.3


static const char* TAG = "SOFTAP";
static EventGroupHandle_t wifi_event_group;
#define LISTENQ 2
#define MESSAGE "Hello TCP Client!!"
const int CLIENT_CONNECTED_BIT = BIT0;
const int CLIENT_DISCONNECTED_BIT = BIT1;
const int AP_STARTED_BIT = BIT2;
uint16_t apCount = 0;

esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch (event->event_id) {
	    case SYSTEM_EVENT_AP_START:
	    	ESP_LOGI("EVENT","AP_START");
	    	xEventGroupSetBits(wifi_event_group, AP_STARTED_BIT);
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

	    case SYSTEM_EVENT_SCAN_DONE:
	        esp_wifi_scan_get_ap_num(&apCount);
	        printf("Number of access points found: %d\n",event->event_info.scan_done.number);
	        if (apCount == 0) {
	           return ESP_OK;
	        }
	        wifi_ap_record_t *list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
	        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));
	        int i;
	        printf("======================================================================\n");
	        printf("             SSID             |    RSSI    |           AUTH           \n");
	        printf("======================================================================\n");
	        for (i=0; i<apCount; i++) {
	           char *authmode;
	           switch(list[i].authmode) {
	              case WIFI_AUTH_OPEN:
	                 authmode = "WIFI_AUTH_OPEN";
	                 break;
	              case WIFI_AUTH_WEP:
	                 authmode = "WIFI_AUTH_WEP";
	                 break;
	              case WIFI_AUTH_WPA_PSK:
	                 authmode = "WIFI_AUTH_WPA_PSK";
	                 break;
	              case WIFI_AUTH_WPA2_PSK:
	                 authmode = "WIFI_AUTH_WPA2_PSK";
	                 break;
	              case WIFI_AUTH_WPA_WPA2_PSK:
	                 authmode = "WIFI_AUTH_WPA_WPA2_PSK";
	                 break;
	              default:
	                 authmode = "Unknown";
	                 break;
	           }
	           printf("%26.26s    |    % 4d    |    %22.22s\n",list[i].ssid, list[i].rssi, authmode);
	        }
	        free(list);
	        printf("\n\n");
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
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
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


void tcp_server(void *pvParam){
    ESP_LOGI(TAG,"tcp_server task started \n");
    struct sockaddr_in tcpServerAddr;
    tcpServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);//don't work with custom ip somehow?
	tcpServerAddr.sin_family = AF_INET;
	tcpServerAddr.sin_port = htons( 100 );
    int s, r;
    char recv_buf[64];
    static struct sockaddr_in remote_addr;
    remote_addr.sin_addr.s_addr = htonl((u32_t)0xC0A80102);
    static unsigned int socklen;
    socklen = sizeof(remote_addr);
    int cs;//client socket
    xEventGroupWaitBits(wifi_event_group,AP_STARTED_BIT,false,true,portMAX_DELAY);
    while(1){
        s = socket(AF_INET, SOCK_STREAM, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket\n");
         if(bind(s, (struct sockaddr *)&tcpServerAddr, sizeof(tcpServerAddr)) != 0) {
            ESP_LOGE(TAG, "... socket bind failed errno=%d \n", errno);
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket bind done \n");
        if(listen (s, LISTENQ) != 0) {
            ESP_LOGE(TAG, "... socket listen failed errno=%d \n", errno);
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        while(1){
        	ESP_LOGI(TAG,"Enter Socket Listening loop");
            cs=accept(s,(struct sockaddr *)&remote_addr, &socklen);
            ESP_LOGI(TAG,"New connection request,Request data:");
            //set O_NONBLOCK so that recv will return, otherwise we need to impliment message end
            //detection logic. If know the client message format you should instead impliment logic
            //detect the end of message
            fcntl(cs,F_SETFL,O_NONBLOCK);
            do {
                bzero(recv_buf, sizeof(recv_buf));
                r = recv(cs, recv_buf, sizeof(recv_buf)-1,0);
                for(int i = 0; i < r; i++) {
                    putchar(recv_buf[i]);
                }
            } while(r > 0);

            ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);

            if( write(cs , MESSAGE , strlen(MESSAGE)) < 0)
            {
                ESP_LOGE(TAG, "... Send failed \n");
                close(s);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "... socket send success");
            close(cs);
        }
        ESP_LOGI(TAG, "... server will be opened in 5 seconds");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "...tcp_client task closed\n");
}

void app_main(void)
{
	 ESP_LOGI(TAG, "[APP] Startup..");
	 ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	 ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	 start_dhcp_server();
	 wifi_init();
	 xTaskCreate(&tcp_server,"tcp_server",4096,NULL,5,NULL);
	 xTaskCreate(&print_sta_info,"print_sta_info",4096,NULL,5,NULL);

	 // Let us test a WiFi scan ...
	   wifi_scan_config_t scanConf = {
	      .ssid = NULL,
	      .bssid = NULL,
	      .channel = 0,
	      .show_hidden = true
	   };
// Create a task to do the scan when it needs to do it
	   while(true){
	         ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));    //The true parameter cause the function to block until
	                                                                   //the scan is done.
	         vTaskDelay(1000 / portTICK_PERIOD_MS);
	     }

}
