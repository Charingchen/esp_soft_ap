/*
 * ESPIDF v4.1 wifi and socket implemnetation
 *
 */


#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "ping/ping_sock.h"
#include "ping/ping.h"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

// Self defined event group to control socket send and receiving read
static EventGroupHandle_t socket_status;
//static const int SCAN_DONE = BIT2;
//static const int READY_TO_SEND = BIT3;


/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int WIFI_CONNECTED_BIT = BIT1;

static const char *TAG = "testing";


#define CMD 99 // char "c"
#define EN_SCAN 49 //(int)"1"
#define EN_TX 50//(int)"2"
#define RECV_PSWD 51//(int)"3"
#define CMD_RETRY "RETRY"

#define CMD_RECEIVED  "CMD received!"
#define SEND_FAIL  "Got NO CMD!"

int scan_done = 0;
char* info_tosend;
int ready_send = 0;
//initial AP count value
uint16_t apCount = 0;

/** event declarations */
typedef enum {
    SC_EVENT_SCAN_DONE,                /*!< ESP32 station smartconfig has finished to scan for APs */
    SC_EVENT_FOUND_CHANNEL,            /*!< ESP32 station smartconfig has found the channel of the target AP */
    SC_EVENT_GOT_SSID_PSWD,            /*!< ESP32 station smartconfig got the SSID and password */
    SC_EVENT_SEND_ACK_DONE,            /*!< ESP32 station smartconfig has sent ACK to cellphone */
} event_t;

/** @brief smartconfig event base declaration. It will go look for "SC_EVENT_**" in the enum defination*/
ESP_EVENT_DECLARE_BASE(SC_EVENT);

// Defind socket port for tcp server
#define PORT 3333

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


// Since we don't have Header file, you need declare before reference
//static void cmd_dectection(void * parm);



static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
//        xTaskCreate(cmd_dectection, "cmd_dectection_task", 4096, NULL, 3, NULL);
    	ESP_LOGI(TAG, "STA Start");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "STA Disconnected");
    }
    // This print out the assigned to the ESP during station setting
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
   }
    // Handle when the scan station event id done
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE){
		esp_wifi_scan_get_ap_num(&apCount);
		printf("Number of access points found: %d\n",apCount);
//		xEventGroupSetBits(socket_status, SCAN_DONE);
		scan_done = 1;
		printf("Scan Done");

      }
//        else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
//        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);

//    }

// Handle if there is a station connects to us
   else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED){
	wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
	ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
			 MAC2STR(event->mac), event->aid);
   }
   // Handle if the connected station is disconnected
   else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED){
	wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
	ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
			 MAC2STR(event->mac), event->aid);
   }




    // self defined events to go through
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");
        vTaskDelete(NULL); // Delete the TCP task
        wifi_config_t *wifi_config = (wifi_config_t*) event_data;
        printf("\n---Disconnecting\n");
		ESP_ERROR_CHECK(esp_wifi_disconnect());
		ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
		ESP_ERROR_CHECK( esp_wifi_connect() );

		ESP_LOGI(TAG, "Testing connection to ping espressif.com");
		//Try to ping espressif.com
		ping_start();


    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
//        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    socket_status = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // New netif has to do create both instance using ap and sta if using softap
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );

    ESP_ERROR_CHECK( esp_wifi_start() );
}


static int cmd_dectection(char* input)
{
	char tempstring[100];
	// If the sending 10 station info would trigger watchdog timeout somehow
	int sta_num = 2;
	char start_msg[5];
	char *start, *seperator;

//	wifi_ap_record_t *list = (wifi_ap_record_t *) malloc(
//			sizeof(wifi_ap_record_t) * apCount);
	wifi_ap_record_t list[10];
	info_tosend = (char *) malloc(sizeof(tempstring) * (sta_num + 1) + 8);
	//add number of station transmitting to the string
	sprintf(start_msg, "%i#", sta_num);
	strcpy(info_tosend, start_msg);

//	xEventGroupClearBits(socket_status, READY_TO_SEND);
	ready_send = 0;
	if (input[0] != CMD) {
		printf("\n Unknown CMD\n");
		return 0;
	}

	else{

		switch (input[1]) {
		case EN_SCAN:
			printf("\nStart WIFI scan\n");
			scan_done = 0;
			ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
			break;
		case EN_TX:
			if (scan_done == 0){
				return 2; // Indicate re-try the same command since the scan is not down
			}
			else{
//				xEventGroupWaitBits(socket_status,SCAN_DONE,false,true,portMAX_DELAY);
				ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));
				for (int i=0; i<sta_num; i++) {
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
//				info_tosend = "";
				printf("info_tosend:\n");
				printf(info_tosend);
//				free(list);
//				printf("\nSelect WIFI and start the SSID password transmitting\n");
//				xEventGroupSetBits(socket_status, READY_TO_SEND);
				ready_send = 1;
				ESP_LOGI(TAG,"Select WIFI and start the SSID password transmitting\n");
				break;
			}


		case RECV_PSWD:
			//deleting TCP task to stop sending and receiving cmd
//			vTaskDelete(NULL);
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

			ESP_ERROR_CHECK(esp_event_post_to(event_handler, SC_EVENT, SC_EVENT_GOT_SSID_PSWD, &wifi_config, sizeof(wifi_config), portMAX_DELAY));

//			wifi_config.sta.bssid_set = evt->bssid_set;
//			if (wifi_config.sta.bssid_set == true) {
//				memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
//			}

//			printf("\n---Disconnecting\n");
//			ESP_ERROR_CHECK(esp_wifi_disconnect());
//			ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
//			ESP_ERROR_CHECK( esp_wifi_connect() );
//
//			ESP_LOGI(TAG, "Testing connection to ping espressif.com");
			//Try to ping espressif.com
//			ping_start();
//		    vTaskDelete( xtcp_server_handle );
			break;



		}
		// Return 1 indicate "c" is received and cmd is received
		return 1;
	}


}

static void send_via_socket (const int sock, const char* send_buffer)
{
	// send() can return less bytes than supplied length.
	// Walk-around for robust implementation.
	int len = strlen(send_buffer);
	printf("\nsend len %d\nSend_buffer:",len);
	printf(send_buffer);
	send(sock, send_buffer,len, 0);
//	int to_write = len;
//	while (to_write > 0) {
//		int written = send(sock, send_buffer + (len - to_write), to_write, 0);
//		if (written < 0) {
//			ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
//		}
//		to_write -= written;
//		printf("\nto_write:%d\n",to_write);
//	}
}

static void do_retransmit(const int sock)
{
    int len,cmd_ret;
    char rx_buffer[128];
//    char* sending_buffer = "2#SSID:TELUS17F7.RSSI:-31.Authmode: WIFI_AUTH_WPA2_PSK.\nSSID:TELUS0129.RSSI:-50.Authmode: WIFI_AUTH_WPA2_PSK.\n";

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE("SOCKET", "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW("SOCKET", "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI("SOCKET", "Received %d bytes: %s", len, rx_buffer);
            // uses if ready then send else skip
            cmd_ret = cmd_dectection(rx_buffer);
            if (cmd_ret== 1){
            	if (ready_send == 1){
            		send_via_socket(sock,info_tosend);
					ESP_LOGI("SOCKET", "Done sending....");
            	}
            	else{
            		ESP_LOGI("SOCKET", "SKIP SENDING");
            	}

             }
            else if(cmd_ret == 2){
				send_via_socket(sock,CMD_RETRY);
				ESP_LOGI("SOCKET", "Sending Retry CMD....");
            }
            else{
             	ESP_LOGI("SOCKET", "CMD not recognized....");
             }
            break;

            }
    } while (len > 0);
    printf("Exiting while loop\n");


}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    } else if (addr_family == AF_INET6) {
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);

    }
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        printf("shutddown socket\n");
        close(sock);
        printf("\nClosed socket\n");
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
}
