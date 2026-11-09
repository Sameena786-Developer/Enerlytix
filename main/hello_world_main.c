#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "mqtt_client.h"
#include "driver/gpio.h"

// ------------ WIFI CONFIGURATION ------------

#define WIFI_SSID       "Wokwi-GUEST"
#define WIFI_PASSWORD   ""

// ------------ MQTT CONFIGURATION ------------

#define MQTT_BROKER     "mqtt://broker.hivemq.com:1883"
#define MQTT_TOPIC      "enerlytix/room1"

#define MQTT_CLIENT_ID  "EnerlytixESP32"

// ------------ RELAY CONFIGURATION ------------

#define RELAY_GPIO      GPIO_NUM_2

#define RELAY_ON_LEVEL  1
#define RELAY_OFF_LEVEL 0

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "ENERLYTIX";

static EventGroupHandle_t wifi_event_group;

static esp_mqtt_client_handle_t mqtt_client = NULL;


// ------------ WIFI EVENT HANDLER ------------

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to WiFi...");
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi disconnected. Reconnecting...");

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "WiFi connected. IP: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


// ------------ WIFI INITIALIZATION ------------

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    strcpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID
    );

    strcpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD
    );

    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization completed");
}


// ------------ MQTT EVENT HANDLER ------------

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:

            ESP_LOGI(TAG, "MQTT connected!");

            esp_mqtt_client_subscribe(
                event->client,
                MQTT_TOPIC,
                0
            );

            ESP_LOGI(TAG, "Subscribed to: %s", MQTT_TOPIC);

            break;


        case MQTT_EVENT_SUBSCRIBED:

            ESP_LOGI(TAG, "MQTT subscription successful");

            break;


        case MQTT_EVENT_DATA:

            ESP_LOGI(
                TAG,
                "Received topic: %.*s",
                event->topic_len,
                event->topic
            );

            ESP_LOGI(
                TAG,
                "Received message: %.*s",
                event->data_len,
                event->data
            );

            // Verify MQTT topic
            if (event->topic_len == strlen(MQTT_TOPIC) &&
                memcmp(
                    event->topic,
                    MQTT_TOPIC,
                    event->topic_len
                ) == 0)
            {
                // Raspberry Pi sends ON
                if (event->data_len == 2 &&
                    memcmp(event->data, "ON", 2) == 0)
                {
                    gpio_set_level(
                        RELAY_GPIO,
                        RELAY_ON_LEVEL
                    );

                    ESP_LOGI(TAG, "PERSON DETECTED");
                    ESP_LOGI(TAG, "RELAY ON");
                }

                // Raspberry Pi sends OFF
                else if (event->data_len == 3 &&
                         memcmp(event->data, "OFF", 3) == 0)
                {
                    gpio_set_level(
                        RELAY_GPIO,
                        RELAY_OFF_LEVEL
                    );

                    ESP_LOGI(TAG, "NO PERSON");
                    ESP_LOGI(TAG, "RELAY OFF");
                }

                else
                {
                    ESP_LOGW(TAG, "Unknown MQTT command");
                }
            }

            break;


        case MQTT_EVENT_DISCONNECTED:

            ESP_LOGW(TAG, "MQTT disconnected");

            break;


        case MQTT_EVENT_ERROR:

            ESP_LOGE(TAG, "MQTT error occurred");

            break;


        default:

            break;
    }
}


// ------------ MQTT INITIALIZATION ------------

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = MQTT_BROKER,
        .credentials.client_id = MQTT_CLIENT_ID,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);

    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "MQTT client initialization failed");
        return;
    }

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(
            mqtt_client,
            ESP_EVENT_ANY_ID,
            mqtt_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_mqtt_client_start(mqtt_client)
    );

    ESP_LOGI(TAG, "MQTT client started");
}


// ------------ MAIN FUNCTION ------------

void app_main(void)
{
    ESP_LOGI(TAG, "Enerlytix AI starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // Initialize relay GPIO
    gpio_reset_pin(RELAY_GPIO);

    gpio_set_direction(
        RELAY_GPIO,
        GPIO_MODE_OUTPUT
    );

    // Keep relay OFF initially
    gpio_set_level(
        RELAY_GPIO,
        RELAY_OFF_LEVEL
    );

    // Initialize WiFi
    wifi_init();

    ESP_LOGI(TAG, "Waiting for WiFi...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    ESP_LOGI(TAG, "WiFi ready");

    // Start MQTT
    mqtt_init();

    ESP_LOGI(TAG, "Enerlytix AI is running");
}