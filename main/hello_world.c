/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

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
#define QUEUE_LENGTH 10
#define SENSOR_READ_INTERVAL_MS 1000

#define sbiSTREAM_BUFFER_LENGTH_BYTES        ( ( size_t ) 1024 )
#define sbiSTREAM_BUFFER_TRIGGER_LEVEL_10    ( ( BaseType_t ) 10 )

static QueueHandle_t queue;
static StreamBufferHandle_t xStreamBuffer = NULL;
static SemaphoreHandle_t write_sem;
static SemaphoreHandle_t read_sem;

// reading the sensor may take some time thus we should use a task instead of timer
void read_sensor(void *pvParameters)
{   
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };

    ultrasonic_init(&sensor);
    float distance;
    while(1)
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
        else{
            xQueueSend(queue, &distance, 0);
        }
        vTaskDelay(SENSOR_READ_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

void pass_value(void *pvParameters)
{   
    while(1)
    {
        float distance;
        xQueueReceive(queue, &distance, portMAX_DELAY);

        xSemaphoreTake(write_sem, portMAX_DELAY);

        if (xStreamBufferSend(xStreamBuffer, &distance, sizeof(float), portMAX_DELAY) != sizeof(float)) {
            ESP_LOGE("pass_value", "Failed to send to buffer\n");
        } else {
            ESP_LOGI("pass_value", "Sent to buffer: %.2f cm\n", distance);
        }

        xSemaphoreGive(read_sem);

        vTaskDelay(SENSOR_READ_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

void print_value(void* pvParameters)
{
    while(1)
    {
        float distance;
        xSemaphoreTake(read_sem, portMAX_DELAY);

        if (xStreamBufferReceive(xStreamBuffer, &distance, sizeof(float), portMAX_DELAY) > 0) {
            ESP_LOGI("print_value", "Received from buffer: %.2f cm\n", distance);
        } else {
            ESP_LOGE("print_value", "Failed to receive from buffer\n");
        }

        xSemaphoreGive(write_sem);
        vTaskDelay(SENSOR_READ_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}


void app_main(void)
{   
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("main", "Starting application...\n");  // A simple print to check if serial output works
    queue = xQueueCreate(QUEUE_LENGTH, sizeof(float));
    if (queue == NULL) {
        ESP_LOGE("main", "Queue creation failed!\n");
        return;
    }
    xStreamBuffer = xStreamBufferCreate( /* The buffer length in bytes. */
        sbiSTREAM_BUFFER_LENGTH_BYTES,
        /* The stream buffer's trigger level. */
        sbiSTREAM_BUFFER_TRIGGER_LEVEL_10 );

    int buffer_capacity_for_floats = sbiSTREAM_BUFFER_LENGTH_BYTES / sizeof(float);
    write_sem = xSemaphoreCreateCounting(buffer_capacity_for_floats, buffer_capacity_for_floats);
    read_sem = xSemaphoreCreateCounting(buffer_capacity_for_floats, 0);

    BaseType_t task1 = xTaskCreate(read_sensor, "read_sensor", 2048, NULL, 2, NULL);
    if (task1 != pdPASS) {
        ESP_LOGE("main", "Failed to create read_sensor task\n");
        return;
    }

    BaseType_t task2 = xTaskCreate(pass_value, "pass_value", 2048, NULL, 2, NULL);
    if (task2 != pdPASS) {
        ESP_LOGE("main", "Failed to create print_value task\n");
        return;
    }

    BaseType_t task3 = xTaskCreate(print_value, "print_value", 2048, NULL, 2, NULL);
    if (task3 != pdPASS) {
        ESP_LOGE("main", "Failed to create print_value task\n");
        return;
    }

    while (1) {
        ESP_LOGI("main", "Running..."); // Periodic log to confirm the app is running
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 1-second delay
    }
}
