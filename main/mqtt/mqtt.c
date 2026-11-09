#include "mqtt.h"

#include <string.h>
#include <stdint.h>

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#define MQTT_BROKER       "mqtt://broker.hivemq.com:1883"
#define MQTT_TOPIC        "enerlytix/room1"
#define MQTT_CLIENT_ID    "EnerlytixESP32"

#define RELAY_PIN         GPIO_NUM_2
#define RELAY_ON_LEVEL    1
#define RELAY_OFF_LEVEL   0

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t client = NULL;

static bool mqtt_connected = false;
static int64_t last_command_time = 0;

static void relay_off(void)
{
    gpio_set_level(RELAY_PIN, RELAY_OFF_LEVEL);
}

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;

    switch (event_id)
    {
        case MQTT_EVENT_CONNECTED:

            mqtt_connected = true;
            last_command_time = 0;

            relay_off();

            ESP_LOGI(TAG, "MQTT connected");

            esp_mqtt_client_subscribe(
                client,
                MQTT_TOPIC,
                1
            );

            ESP_LOGI(TAG, "Subscribed to %s", MQTT_TOPIC);

            break;

        case MQTT_EVENT_DATA:

            // Reject fragmented MQTT messages.
            if (event->current_data_offset != 0 ||
                event->data_len != event->total_data_len)
            {
                ESP_LOGW(TAG, "Incomplete MQTT message ignored");
                break;
            }

            // Validate topic.
            if (event->topic_len != strlen(MQTT_TOPIC) ||
                strncmp(event->topic, MQTT_TOPIC,
                        event->topic_len) != 0)
            {
                break;
            }

            // Validate command and control relay.
            if (event->data_len == 2 &&
                strncmp(event->data, "ON", 2) == 0)
            {
                gpio_set_level(RELAY_PIN, RELAY_ON_LEVEL);

                last_command_time = esp_timer_get_time() / 1000;

                ESP_LOGI(TAG, "Command received: ON");
                ESP_LOGI(TAG, "Relay ON");
            }
            else if (event->data_len == 3 &&
                     strncmp(event->data, "OFF", 3) == 0)
            {
                gpio_set_level(RELAY_PIN, RELAY_OFF_LEVEL);

                last_command_time = esp_timer_get_time() / 1000;

                ESP_LOGI(TAG, "Command received: OFF");
                ESP_LOGI(TAG, "Relay OFF");
            }
            else
            {
                ESP_LOGW(TAG, "Unknown MQTT command ignored");
            }

            break;

        case MQTT_EVENT_DISCONNECTED:

            mqtt_connected = false;
            last_command_time = 0;

            relay_off();

            ESP_LOGW(TAG, "MQTT disconnected. Relay OFF");

            break;

        case MQTT_EVENT_ERROR:

            mqtt_connected = false;
            last_command_time = 0;

            relay_off();

            ESP_LOGE(TAG, "MQTT connection error. Relay OFF");

            break;

        default:
            break;
    }
}

void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = MQTT_BROKER,
        .credentials.client_id = MQTT_CLIENT_ID,
    };

    client = esp_mqtt_client_init(&mqtt_config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "MQTT client initialization failed");
        return;
    }

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(
            client,
            ESP_EVENT_ANY_ID,
            mqtt_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
}

bool mqtt_is_connected(void)
{
    return mqtt_connected;
}

int64_t mqtt_get_last_command_time(void)
{
    return last_command_time;
}