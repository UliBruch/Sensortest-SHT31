/**
 * @file reaction_task.h
 * @brief Reaktionstest-Task: State Machine, Taster und LED
 */

#ifndef REACTION_TASK_H
#define REACTION_TASK_H

#include "esp_err.h"

/**
 * @brief Initialisiert Taster und LED und startet den Reaktionstest-Task.
 *
 * Setzt voraus, dass das Display bereits über display_init() initialisiert
 * wurde, da der Task direkt auf die Display-Funktionen zugreift.
 *
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t reaction_task_start(void);

#endif // REACTION_TASK_H
