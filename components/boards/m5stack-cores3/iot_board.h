/**
 * M5Stack CoreS3 Board Definitions
 */

#ifndef _IOT_BOARD_H_
#define _IOT_BOARD_H_

#include "board_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== GPIO Definitions ==================== */
// I2C
#define BOARD_I2C_SDA             12
#define BOARD_I2C_SCL             11

// I2S Audio Output
#define BOARD_I2S_BCK             34
#define BOARD_I2S_WS              33
#define BOARD_I2S_DOUT            14
#define BOARD_I2S_MCLK            0

// I2C Device Addresses (on shared I2C bus)
#define BOARD_AW88298_ADDR        0x36  // Audio DAC/Amp
#define BOARD_AW9523B_ADDR        0x58  // IO Expander for RST control

/**
 * Initialize board hardware (GPIO, I2C, DAC)
 */
esp_err_t iot_board_init(void);

/**
 * Deinitialize board hardware
 */
esp_err_t iot_board_deinit(void);

/**
 * Check if board is initialized
 */
bool iot_board_is_init(void);

/**
 * Get handle to board resource (I2C bus, SPI bus, etc.)
 */
typedef void* board_res_handle_t;
board_res_handle_t iot_board_get_handle(int id);

/**
 * Get board information string
 */
const char *iot_board_get_info(void);

#ifdef __cplusplus
}
#endif

#endif // _IOT_BOARD_H_
