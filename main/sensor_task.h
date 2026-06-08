/**
 * @file sensor_task.h
 * @brief Sensor-Task: liest zyklisch den SHT31 und zeigt die Werte an
 */

#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "esp_err.h"

/**
 * @brief Startet den Sensor-Task.
 *
 * Setzt voraus, dass Display (display_init) und Sensor (sht31_init) bereits
 * initialisiert wurden, da der Task direkt darauf zugreift.
 *
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t sensor_task_start(void);

#endif // SENSOR_TASK_H
