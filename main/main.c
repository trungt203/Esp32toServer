#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "Wifi.h"
#include "nvs_flash.h"
#include "esp_websocket_client.h"
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include <time.h>


static char *TAG = "main";

struct data_send{
    char name[20]; //can repeat name
    bool active;
    char device_id[10]; ; //1 id 1 device
    bool isOn;
};

uint8_t amount;
void setStruct(struct data_send *datas);

#define Bulb_1 2

esp_websocket_client_handle_t client;
uint16_t counter = 0;
uint16_t counterLim = 0;
esp_timer_handle_t periodic_timer;
SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
bool isTimerRunning = false;

void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void websocket_app_start();
cJSON *payload(char *action, struct data_send *datas);
void sendRequest(char *action, struct data_send *datas);
void handleAction(char *action);
static void periodic_timer_callback(void* arg);



void app_main(void)
{
    //Config button
    gpio_reset_pin(Bulb_1);
    gpio_set_direction(Bulb_1,GPIO_MODE_OUTPUT);

    vTaskDelay(200/ portTICK_PERIOD_MS);
    //Config so luong bulb
    int a = 1;
    struct data_send datas[a];

    //Done config

    //Config connect Webserver
    const char *ssid = "TrungPhat";
    const char *password = "30090610";
    const char *gateway = "ws://nhacnkd.online:8080";

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //timer config
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            .name = "periodic"
    };

    //timer, semephore timer 
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    timerSemaphore = xSemaphoreCreateBinary();

    //require event loop to check wifi and websocket event
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    connectWifi(ssid, password);
    websocket_app_start(gateway);\
    // xTaskCreate(ButtonOccur, "Xu_ly_nut_nhan",4096, NULL, 1, NULL);
    // xTaskCreate(checkStateTask, "checkStateTask", 2048, NULL, 1, NULL);
    
    // xTaskCreate(PrintOLED, "In man hinh", 2048, NULL, 1, NULL);
    // xTaskCreate(CheckStateButton, "Trang thai nut nhan", 2048, NULL,1,NULL);
}

void setStruct(struct data_send *datas){
    datas[0].active = false;
    sprintf(datas[0].device_id,"Bulb_01");
    sprintf(datas[0].name,"Den so 1");
    datas[0].isOn = false;
}


void websocket_app_start(const char *gateway) {
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = gateway;
    websocket_cfg.reconnect_timeout_ms = 2000;

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
}

void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Websocket connected");
        sendRequest("join", true);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Websocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        cJSON *json;
        json = cJSON_Parse(data->data_ptr);
        if (data->data_len == 0 || json == NULL) {
            return;
        }
        if (cJSON_IsObject(json)) {
            cJSON *sender = cJSON_GetObjectItemCaseSensitive(json, "sender");
            if (sender != NULL && cJSON_IsString(sender) && !strcmp(sender->valuestring, "server")) {

                ESP_LOGI(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);

                cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
                if (action != NULL && cJSON_IsString(action)) {
                    handleAction(action->valuestring);
                }
            }
        }

        cJSON_Delete(json);
        break;
    }
}

cJSON *payload(char *action,struct data_send *datas) {
    //Data: name, device, active
    cJSON *json, *deviceList;
    json = cJSON_CreateObject();
    deviceList = cJSON_CreateArray();

    for (uint8_t i = 0; i < amount; ++i) {
        char  a[70];
        sprintf(a, "name: %s, device_id: %s, active: %d",datas[i].name, datas[i].device_id, datas[i].active );
        cJSON_AddItemToArray(deviceList, cJSON_CreateString(a));
      } 
    cJSON_AddItemToObject(json, "data", deviceList);

    cJSON_AddStringToObject(json, "action", action);
    cJSON_AddStringToObject(json, "sender", "devices");

    char *js = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    return js;

}

void sendRequest(char *action, struct data_send *datas) {
    if (esp_websocket_client_is_connected(client)) {
      const char *json = payload(action, datas);
      esp_websocket_client_send_text(client, json, strlen(json), portMAX_DELAY);
    }
}

static void periodic_timer_callback(void* arg)
{
    portENTER_CRITICAL_ISR(&timerMux);
    counter++;
    portEXIT_CRITICAL_ISR(&timerMux);
    xSemaphoreGiveFromISR(timerSemaphore, NULL);
}

void handleAction(char *action) {
    struct data_send datas[amount];
    setStruct(datas);

    //Xu ly event cua device duoc gui 
    for(int i=0; i< amount; i = i+1){
        if(!strcmp(action, datas[i].device_id) ) {
            datas[i].isOn = !datas[i].isOn;
            gpio_set_level(Bulb_1, datas[i].isOn);
            datas[i].active = true;

        }
    }
    
    sendRequest(action, datas);
}


void checkStateTask() {
    while(1) {
        if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
            ESP_LOGI(TAG, "counter: %d", counter);
            if (counter >= counterLim) {
                ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
                counter = 0;
                counterLim = 0;
                isTimerRunning = false;
            
            }
        }

        
    }

    vTaskDelete( NULL );
}
