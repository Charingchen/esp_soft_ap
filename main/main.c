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

#include "ping/ping_sock.h"
#include "ping/ping.h"


#define SSID "ESP32AP"
//#define customIp ((u32_t)0xC0A80103UL) //102.168.1.3
#define CMD 99 // char "c"
#define EN_SCAN 49 //(int)"1"
#define EN_TX 50//(int)"2"
#define RECV_PSWD 51//(int)"3"

static const char* TAG = "ESP WIFI provision";
static EventGroupHandle_t wifi_event_group;
#define LISTENQ 2
#define CMD_RECEIVED  "CMD received!"
#define SEND_FAIL  "Got NO CMD!"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

const int CLIENT_CONNECTED_BIT = BIT0;
const int CLIENT_DISCONNECTED_BIT = BIT1;
const int AP_STARTED_BIT = BIT2;

TaskHandle_t xtcp_server_handle = NULL;

int scan_done = 0;
char* info_tosend;
int ready_send = 0;
//initial AP count value
uint16_t apCount = 0;

/** Smartconfig event declarations */
typedef enum {
    SC_EVENT_SCAN_DONE,                /*!< ESP32 station smartconfig has finished to scan for APs */
    SC_EVENT_FOUND_CHANNEL,            /*!< ESP32 station smartconfig has found the channel of the target AP */
    SC_EVENT_GOT_SSID_PSWD,            /*!< ESP32 station smartconfig got the SSID and password */
    SC_EVENT_SEND_ACK_DONE,            /*!< ESP32 station smartconfig has sent ACK to cellphone */
} smartconfig_event_t;

/** @brief smartconfig event base declaration */
ESP_EVENT_DECLARE_BASE(SC_EVENT);

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
	        if (apCount == 0) {
	           return ESP_OK;
	        }
	        printf("Number of access points found: %d\n",event->event_info.scan_done.number);
	        scan_done = 1;
	        printf("scan_done: %i", scan_done);
	        break;
	    case SYSTEM_EVENT_STA_START:
	            esp_wifi_connect();
	            break;
	    case SYSTEM_EVENT_STA_GOT_IP:
//	            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
//	                    if (!g_station_list) {
//	                g_station_list = malloc(sizeof(station_info_t));
//	                g_station_list->next = NULL;
//	                ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
//	                ESP_ERROR_CHECK(esp_wifi_set_promiscuous(1));

	          break;
	     case SYSTEM_EVENT_STA_DISCONNECTED:
	            esp_wifi_connect();
	            //xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
	            break;
	    default:
	        break;
	    }
    return ESP_OK;
}

//static void start_dhcp_server(){
//
//    	// initialize the tcp stack
//	    tcpip_adapter_init();
//        // stop DHCP server
//        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
//        // assign a static IP to the network interface
//        tcpip_adapter_ip_info_t info;
//        memset(&info, 0, sizeof(info));
//        IP4_ADDR(&info.ip, 192, 168, 1, 1);
//        IP4_ADDR(&info.gw, 192, 168, 1, 1);//ESP acts as router, so gw addr will be its own addr
//        IP4_ADDR(&info.netmask, 255, 255, 255, 0);
//        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
//        // start the DHCP server
//        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
//        printf("DHCP server started \n");
//}

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

//	wifi_event_group = xEventGroupCreate();
//	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
//	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
//	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
//	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
//
//    ESP_ERROR_CHECK( esp_wifi_start() );
//    printf("ESP WiFi started in APSTA mode \n");

    ESP_ERROR_CHECK(esp_netif_init());
	s_wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
	assert(sta_netif);

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

	ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
	ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
	ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_start() );

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

/*
 * ESP32 ICMP Echo
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/icmp_echo.html
 */
static void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    printf("%d bytes from %s icmp_seq=%d ttl=%d time=%d ms\n",
           recv_len, ipaddr_ntoa((ip_addr_t*)&target_addr), seqno, ttl, elapsed_time);
}

static void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    printf("From %s icmp_seq=%d timeout\n",ipaddr_ntoa((ip_addr_t*)&target_addr), seqno);
}

static void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    ip_addr_t target_addr;
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    uint32_t loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
    if (IP_IS_V4(&target_addr)) {
        printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
    } else {
        printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
    }
    printf("%d packets transmitted, %d received, %d%% packet loss, time %dms\n",
           transmitted, received, loss, total_time_ms);
    // delete the ping sessions, so that we clean up all resources and can create a new ping session
    // we don't have to call delete function in the callback, instead we can call delete function from other tasks
    esp_ping_delete_session(hdl);
}


int ping_start()
{
	esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();


	// parse IP address
	struct sockaddr_in6 sock_addr6;
	ip_addr_t target_addr;
	memset(&target_addr, 0, sizeof(target_addr));
	char *ping_target = "www.espressif.com";

	if (inet_pton(AF_INET6, ping_target, &sock_addr6.sin6_addr) == 1) {
		/* convert ip6 string to ip6 address */
		ipaddr_aton(ping_target, &target_addr);
	} else {
		struct addrinfo hint;
		struct addrinfo *res = NULL;
		memset(&hint, 0, sizeof(hint));
		/* convert ip4 string or hostname to ip4 or ip6 address */
		if (getaddrinfo(ping_target, NULL, &hint, &res) != 0) {
			printf("ping: unknown host %s\n", ping_target);
			return 1;
		}
		if (res->ai_family == AF_INET) {
			struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
			inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
		} else {
			struct in6_addr addr6 = ((struct sockaddr_in6 *) (res->ai_addr))->sin6_addr;
			inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6);
		}
		freeaddrinfo(res);
	}
	config.target_addr = target_addr;

	/* set callback functions */
	esp_ping_callbacks_t cbs = {
		.on_ping_success = cmd_ping_on_ping_success,
		.on_ping_timeout = cmd_ping_on_ping_timeout,
		.on_ping_end = cmd_ping_on_ping_end,
		.cb_args = NULL
	};
	esp_ping_handle_t ping;
	esp_ping_new_session(&config, &cbs, &ping);

    ESP_LOGI(TAG, "Ping start");
    ESP_ERROR_CHECK(esp_ping_start(ping));
    ESP_LOGI(TAG, "Ping end");
    return 0;

}


int cmd_detection(const char* input) {
//	static unsigned int input_len;
//	input_len = sizeof(input);
	int i;
	char tempstring[100];
	int sta_num = 10;
	char start_msg[5];
	char *start, *seperator;

	wifi_ap_record_t *list = (wifi_ap_record_t *) malloc(
			sizeof(wifi_ap_record_t) * apCount);
	info_tosend = (char *) malloc(sizeof(tempstring) * (sta_num + 1) + 8);
	//add number of station transmitting to the string
	sprintf(start_msg, "%i#", sta_num);
	strcpy(info_tosend, start_msg);
//	if (input_len != 4){
//		 ESP_LOGI("CMD", "Not a Command, jumping out....");
//		 return -1;
//	}

	if (input[0] != CMD) {
		printf("\n Unknown CMD\n");
		return 0;
	}

	else{

		switch (input[1]) {
		case EN_SCAN:
			printf("\nStart WIFI scan\n");
//			ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
			ready_send=0;
			//scan_done = 0;
			break;
		case RECV_PSWD:
			//deleting TCP task to stop sending and receiving cmd
			//pointer to the password portion of the input

			start = strstr(input,"ssid:")+5;
			seperator = strstr(input,".");
			uint8_t ssid [33];
			uint8_t pwd[65];
			memcpy(ssid,start,seperator - start);
			memcpy(pwd,seperator+5,sizeof(pwd));

			printf("ssid--%s\npwd--%s",ssid,pwd);

			wifi_config_t wifi_config;
			bzero(&wifi_config, sizeof(wifi_config_t));
			memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
			memcpy(wifi_config.sta.password, pwd, sizeof(wifi_config.sta.password));

//			wifi_config.sta.bssid_set = evt->bssid_set;
//			if (wifi_config.sta.bssid_set == true) {
//				memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
//			}

			printf("\n---Disconnecting\n");
			ESP_ERROR_CHECK(esp_wifi_disconnect());
			ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
			ESP_ERROR_CHECK( esp_wifi_connect() );

		    ESP_LOGI(TAG, "Testing connection to ping espressif.com");
		    //Try to ping espressif.com
		    ping_start();
//		    vTaskDelete( xtcp_server_handle );
			break;

		case EN_TX:
			if (scan_done == 1){
				ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));
				for (i=0; i<sta_num; i++) {
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
				   sprintf(tempstring,"SSID:%s.RSSI:%3d.Authmode: %s.\n",list[i].ssid, list[i].rssi, authmode);
				   printf(tempstring);
				   strcat(info_tosend, tempstring);

				}
				printf("info_tosend:\n");
				printf(info_tosend);
				free(list);
				ready_send = 1;
				scan_done = 0;
				printf("\nSelect WIFI and start the SSID password transmitting\n");
				break;
			}
			else{
				ESP_LOGI("WIFISCAN", "No Scan Done yet.");
				printf("scan_done: %i", scan_done);
			}
		}
		// Return 1 indicate "c" is received and cmd is received
		return 1;
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
    int cmd_recv = 0;

 //   char* cmd_recv;

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
            //set O_NONBLOCK so that recv will return, otherwise we need to implement message end
            //detection logic. If know the client message format you should instead implement logic
            //detect the end of message
            fcntl(cs,F_SETFL,O_NONBLOCK);

            // need to do keep Reading until get the message catch logic
            do {
                bzero(recv_buf, sizeof(recv_buf));
                r = recv(cs, recv_buf, sizeof(recv_buf)-1,0);


                if (r>0){
                	if (cmd_detection(recv_buf) ==1){
                		cmd_recv = 1;
                		break;
                	}
                }
                for(int i = 0; i < r; i++) {
                    putchar(recv_buf[i]);
                }
            } while(r > 0);

            ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);

            if (cmd_recv == 1){
            	// If the cmd is recieved and out going message is ready
            	if (ready_send == 1){
            		if( write(cs ,info_tosend , strlen(info_tosend)) < 0){
						ESP_LOGE(TAG, "... Send failed \n");
						close(s);
						vTaskDelay(4000 / portTICK_PERIOD_MS);
						continue;
					}
					ESP_LOGI(TAG, "... socket send success");
					close(cs);
					cmd_recv = 0;
					free(info_tosend);

					ready_send = 0;
            	}

            	else{
            		if( write(cs ,CMD_RECEIVED , strlen(CMD_RECEIVED)) < 0)
					{
						ESP_LOGE(TAG, "... Send failed \n");
						close(s);
						vTaskDelay(4000 / portTICK_PERIOD_MS);
						continue;
					}
					ESP_LOGI(TAG, "... socket send success");
					close(cs);
					cmd_recv = 0;
            	}

            }

            else {
            	if( write(cs ,SEND_FAIL, strlen(SEND_FAIL)) < 0)
				{
					ESP_LOGE(TAG, "... Send failed (sending fail cmd) \n");
					close(s);
					vTaskDelay(4000 / portTICK_PERIOD_MS);
					continue;
				}
				ESP_LOGI(TAG, "... socket send success");
				close(cs);
            }
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
	 nvs_flash_init();
//	 start_dhcp_server();
	 wifi_init();
	 xTaskCreate(&tcp_server,"tcp_server",4096,NULL,5,&xtcp_server_handle);
	 xTaskCreate(&print_sta_info,"print_sta_info",4096,NULL,5,NULL);


// Create a task to do the scan when it needs to do it
//	   while(true){
//	         ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));    //The true parameter cause the function to block until
//	                                                                   //the scan is done.
//	         vTaskDelay(1000 / portTICK_PERIOD_MS);
//	     }

}
