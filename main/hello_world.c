#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ultrasonic.h>
#include <esp_err.h>
#include "esp_log.h"

#define MAX_DISTANCE_CM 200 // 5m max
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18
#define SENSOR_READ_INTERVAL_MS 1000

#define sbiSTREAM_BUFFER_LENGTH_BYTES        ( ( size_t ) 1024 )
#define sbiSTREAM_BUFFER_TRIGGER_LEVEL_10    ( ( BaseType_t ) 10 )

static StreamBufferHandle_t xStreamBuffer = NULL;
static SemaphoreHandle_t write_sem;
static SemaphoreHandle_t read_sem;
TaskHandle_t task2_handle;


void read_sensor(void *pvParameters)
{   
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };
    float distance;
    ultrasonic_init(&sensor);

    while (1)
    {
        
        esp_err_t res = ultrasonic_measure(&sensor, MAX_DISTANCE_CM, &distance);
        
        if (res != ESP_OK)
        {
            ESP_LOGE("read_sensor", "Error %d: ", res);
            switch (res)
            {
                case ESP_ERR_ULTRASONIC_PING:
                    ESP_LOGE("read_sensor", "Cannot ping (device is in invalid state)\n");
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    ESP_LOGE("read_sensor", "Ping timeout (no device found)\n");
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    ESP_LOGE("read_sensor", "Echo timeout (i.e. distance too big)\n");
                    break;
                default:
                    ESP_LOGE("read_sensor", "%s\n", esp_err_to_name(res));
            }
        }
        else
        {
            ESP_LOGI("read_sensor", "Measured distance: %.2f cm", distance);

            // Convert float to uint32_t and send via task notification
            uint32_t distance_int = *(uint32_t *)&distance; 
            xTaskNotify(task2_handle, distance_int, eSetValueWithOverwrite);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

void pass_value(void *pvParameters)
{
    while (1)
    {
        uint32_t distance_int;
        xTaskNotifyWait(0, 0, &distance_int, portMAX_DELAY);

        float distance = *(float *)&distance_int; // Convert back to float
        ESP_LOGI("pass_value", "Received distance: %.2f cm", distance);

        xSemaphoreTake(write_sem, portMAX_DELAY);

        if (xStreamBufferSend(xStreamBuffer, &distance, sizeof(float), portMAX_DELAY) != sizeof(float)) 
        {
            ESP_LOGE("pass_value", "Failed to send to buffer\n");
        } 
        else 
        {
            ESP_LOGI("pass_value", "Sent to buffer: %.2f cm\n", distance);
        }

        xSemaphoreGive(read_sem);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

void print_value(void* pvParameters)
{
    while (1)
    {
        float distance;
        xSemaphoreTake(read_sem, portMAX_DELAY);

        if (xStreamBufferReceive(xStreamBuffer, &distance, sizeof(float), portMAX_DELAY) > 0) 
        {
            ESP_LOGI("print_value", "Received from buffer: %.2f cm\n", distance);
        } 
        else 
        {
            ESP_LOGE("print_value", "Failed to receive from buffer\n");
        }

        xSemaphoreGive(write_sem);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("main", "Starting application...\n");  

    xStreamBuffer = xStreamBufferCreate(sbiSTREAM_BUFFER_LENGTH_BYTES, sbiSTREAM_BUFFER_TRIGGER_LEVEL_10);

    int buffer_capacity_for_floats = sbiSTREAM_BUFFER_LENGTH_BYTES / sizeof(float);
    write_sem = xSemaphoreCreateCounting(buffer_capacity_for_floats, buffer_capacity_for_floats);
    read_sem = xSemaphoreCreateCounting(buffer_capacity_for_floats, 0);

    BaseType_t task2 = xTaskCreate(pass_value, "pass_value", 2048, NULL, 2, &task2_handle);
    if (task2 != pdPASS) 
    {
        ESP_LOGE("main", "Failed to create pass_value task\n");
        return;
    }

    BaseType_t task1 = xTaskCreate(read_sensor, "read_sensor", 2048, NULL, 2, NULL);
    if (task1 != pdPASS) 
    {
        ESP_LOGE("main", "Failed to create read_sensor task\n");
        return;
    }

    BaseType_t task3 = xTaskCreate(print_value, "print_value", 2048, NULL, 2, NULL);
    if (task3 != pdPASS) 
    {
        ESP_LOGE("main", "Failed to create print_value task\n");
        return;
    }

    while (1) 
    {
        ESP_LOGI("main", "Running..."); 
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}