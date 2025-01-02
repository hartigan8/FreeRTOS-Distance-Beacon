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



#define MAX_DISTANCE_CM 200 // 5m max
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18
#define QUEUE_LENGTH 10
#define SENSOR_READ_INTERVAL_MS 1000

static QueueHandle_t queue;

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
            printf("Error %d: ", res);
            switch (res)
            {
                case ESP_ERR_ULTRASONIC_PING:
                    printf("Cannot ping (device is in invalid state)\n");
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    printf("Ping timeout (no device found)\n");
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    printf("Echo timeout (i.e. distance too big)\n");
                    break;
                default:
                    printf("%s\n", esp_err_to_name(res));
            }
        }
        else{
            xQueueSend(queue, &distance, 0);
        }
        vTaskDelay(SENSOR_READ_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

void print_value(void *pvParameters)
{   
    float distance;
    while(1)
    {
        
        xQueueReceive(queue, &distance, portMAX_DELAY);
        printf("Distance: %.2f cm\n", distance);
        vTaskDelay(SENSOR_READ_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}
void app_main(void)
{
    queue = xQueueCreate(QUEUE_LENGTH, sizeof(float));
    if (queue == NULL) {
        printf("Queue creation failed!\n");
        return;
    }

    printf("Free heap before tasks: %d bytes\n", xPortGetFreeHeapSize());

    BaseType_t task1 = xTaskCreate(read_sensor, "read_sensor", 2048, NULL, 2, NULL);
    if (task1 != pdPASS) {
        printf("Failed to create read_sensor task\n");
        return;
    }

    BaseType_t task2 = xTaskCreate(print_value, "print_value", 2048, NULL, 2, NULL);
    if (task2 != pdPASS) {
        printf("Failed to create print_value task\n");
        return;
    }

    printf("Free heap after tasks: %d bytes\n", xPortGetFreeHeapSize());
}
