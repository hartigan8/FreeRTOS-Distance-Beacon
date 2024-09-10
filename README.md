Requirements:

Task 1: sensor read task which grabs a new reading every second and puts that reading into a FreeRTOS Queue.
Task 2: waits for a new reading in the FreeRTOS Queue and puts the new reading into a shared ring buffer and increments a FreeRTOS Semaphore.
Task 3: checks the FreeRTOS Semaphore and updates the BLE eddystone beacon temperature data with the new reading if available.
