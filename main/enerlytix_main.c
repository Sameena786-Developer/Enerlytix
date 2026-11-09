#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

// ---------------- WIFI CONFIGURATION ----------------

#define WIFI_SSID       "Redmi 10 Prime 2022"
#define WIFI_PASSWORD   "sameena123"

#define WIFI_CONNECTED_BIT BIT0

// ---------------- MQTT CONFIGURATION ----------------

#define MQTT_BROKER_URI "mqtt://broker.hivemq.com:1883"
#define MQTT_TOPIC      "enerlytix/room1"
#define MQTT_CLIENT_ID  "EnerlytixESP32-Room1"

// ---------------- RELAY CONFIGURATION ----------------

#define RELAY_GPIO      GPIO_NUM_26

#define RELAY_ON_LEVEL  1
#define RELAY_OFF_LEVEL 0

// ---------------- WATCHDOG CONFIGURATION ----------------

#define COMMAND_TIMEOUT_MS 15000
#define WATCHDOG_INTERVAL_MS 1000

// ---------------- GLOBAL VARIABLES ----------------

static const char *TAG = "ENERLYTIX";

static EventGroupHandle_t wifi_event_group = NULL;

static esp_mqtt_client_handle_t mqtt_client = NULL;

static volatile bool mqtt_connected = false;
static volatile int64_t last_command_time = 0;

static bool relay_state = false;

// ---------------- RELAY CONTROL ----------------

static void relay_set(bool on)
{
    gpio_set_level(
        RELAY_GPIO,
        on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL
    );

    if (relay_state != on)
    {
        relay_state = on;

        ESP_LOGI(
            TAG,
            "ROOM SUPPLY: %s",
            on ? "ON" : "OFF"
        );
    }
}

// ---------------- WIFI EVENT HANDLER ----------------

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to hotspot...");

        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGE(
            TAG,
            "Wi-Fi disconnected. Reason: %d",
            event->reason
        );

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        mqtt_connected = false;
        last_command_time = 0;

        relay_set(false);

        ESP_LOGW(TAG, "Attempting Wi-Fi reconnection...");

        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Wi-Fi connected successfully!");

        ESP_LOGI(
            TAG,
            "IP Address: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

// ---------------- WIFI INITIALIZATION ----------------

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "Wi-Fi event group creation failed");
        abort();
    }

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID,
        sizeof(wifi_config.sta.ssid)
    );

    strncpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD,
        sizeof(wifi_config.sta.password) - 1
    );

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_ps(WIFI_PS_NONE)
    );

    ESP_LOGI(TAG, "Starting Wi-Fi...");

    ESP_ERROR_CHECK(esp_wifi_start());
}

// ---------------- MQTT EVENT HANDLER ----------------

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;

    switch (event_id)
    {
        case MQTT_EVENT_CONNECTED:

            mqtt_connected = true;
            last_command_time = 0;

            relay_set(false);

            ESP_LOGI(TAG, "MQTT CONNECTED");

            esp_mqtt_client_subscribe(
                mqtt_client,
                MQTT_TOPIC,
                1
            );

            ESP_LOGI(
                TAG,
                "Subscribed to %s",
                MQTT_TOPIC
            );

            break;

        case MQTT_EVENT_DATA:

            // Ignore fragmented messages.

            if (event->current_data_offset != 0 ||
                event->data_len != event->total_data_len)
            {
                ESP_LOGW(TAG, "Incomplete MQTT message ignored");
                break;
            }

            // Validate topic.

            if (event->topic_len != strlen(MQTT_TOPIC) ||
                strncmp(
                    event->topic,
                    MQTT_TOPIC,
                    event->topic_len
                ) != 0)
            {
                break;
            }

            // ON command.

            if (event->data_len == 2 &&
                strncmp(event->data, "ON", 2) == 0)
            {
                last_command_time =
                    esp_timer_get_time() / 1000;

                relay_set(true);

                ESP_LOGI(TAG, "Received MQTT command: ON");
            }

            // OFF command.

            else if (event->data_len == 3 &&
                     strncmp(event->data, "OFF", 3) == 0)
            {
                last_command_time =
                    esp_timer_get_time() / 1000;

                relay_set(false);

                ESP_LOGI(TAG, "Received MQTT command: OFF");
            }

            else
            {
                ESP_LOGW(TAG, "Unknown MQTT command ignored");
            }

            break;

        case MQTT_EVENT_DISCONNECTED:

            mqtt_connected = false;
            last_command_time = 0;

            relay_set(false);

            ESP_LOGW(TAG, "MQTT DISCONNECTED");

            break;

        case MQTT_EVENT_ERROR:

            mqtt_connected = false;
            last_command_time = 0;

            relay_set(false);

            ESP_LOGE(TAG, "MQTT ERROR");

            break;

        default:
            break;
    }
}

// ---------------- MQTT INITIALIZATION ----------------

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_CLIENT_ID
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

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

    ESP_LOGI(TAG, "Starting MQTT client...");

    ESP_ERROR_CHECK(
        esp_mqtt_client_start(mqtt_client)
    );
}

// ---------------- WATCHDOG TASK ----------------

static void watchdog_task(void *arg)
{
    while (1)
    {
        int64_t now = esp_timer_get_time() / 1000;

        int64_t last_command = last_command_time;

        if (!mqtt_connected)
        {
            relay_set(false);
        }

        else if (
            last_command == 0 ||
            (now - last_command) > COMMAND_TIMEOUT_MS
        )
        {
            relay_set(false);
        }

        vTaskDelay(
            pdMS_TO_TICKS(WATCHDOG_INTERVAL_MS)
        );
    }
}

// ---------------- MAIN APPLICATION ----------------

void app_main(void)
{
    ESP_LOGI(TAG, "ENERLYTIX AI STARTING");

    // Initialize NVS.

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // Initialize relay GPIO.

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    relay_set(false);

    ESP_LOGI(TAG, "Relay initialized OFF");

    // Start watchdog.

    BaseType_t task_result = xTaskCreate(
        watchdog_task,
        "watchdog_task",
        2048,
        NULL,
        5,
        NULL
    );

    if (task_result != pdPASS)
    {
        ESP_LOGE(TAG, "Watchdog task creation failed");
        return;
    }

    // Initialize Wi-Fi.

    ESP_LOGI(TAG, "Initializing Wi-Fi...");

    wifi_init();

    ESP_LOGI(TAG, "Waiting for Wi-Fi IP address...");

    // Wait until ESP32 actually obtains an IP address.

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    ESP_LOGI(TAG, "Wi-Fi is ready");

    // Start MQTT only after Wi-Fi connection.

    ESP_LOGI(TAG, "Initializing MQTT...");

    mqtt_init();

    ESP_LOGI(TAG, "ENERLYTIX AI RUNNING");
}