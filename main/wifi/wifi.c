#include "wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_err.h"

#define WIFI_SSID       "Redmi 10 Prime 2022"
#define WIFI_PASSWORD   "sameena123"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_event_group = NULL;

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(
            TAG,
            "Wi-Fi disconnected. Reason: %d",
            event->reason
        );

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        ESP_LOGI(TAG, "Reconnecting to Wi-Fi...");

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

esp_err_t wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "Event group creation failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

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

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(30000)
    );

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Wi-Fi initialization successful");
        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "Wi-Fi connection timed out. Reconnection continues in background."
    );

    return ESP_ERR_TIMEOUT;
}

bool wifi_is_connected(void)
{
    if (wifi_event_group == NULL)
    {
        return false;
    }

    return (
        xEventGroupGetBits(wifi_event_group) &
        WIFI_CONNECTED_BIT
    ) != 0;
}