/**
 * M5Stack CoreS3 Board Support
 * 
 * Audio output chain:
 * ESP32-S3 I2S (44.1/48kHz, Philips) 
 *   -> AW88298 Class-D Amplifier (I2C @0x36)
 *   -> Internal 1W Speaker
 *
 * I2C Control:
 * AW88298 (0x36) - Audio DAC/Amp via AW9523B IO Expander
 * AW9523B (0x58) - GPIO Expansion for RST/INT control
 *
 * I2S Pins (CoreS3 specific):
 * - BCK: GPIO34, WS: GPIO33, DOUT: GPIO14, MCLK: GPIO0
 */

#include "iot_board.h"
#include "board_utils.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dac.h"

static const char *TAG = "board_m5cores3";

/* ==================== I2C Device Addresses ==================== */
#define AW88298_ADDR        0x36  // Audio DAC/Amp
#define AW9523B_ADDR        0x58  // IO Expander
#define I2C_MASTER_SPEED    100000 // 100kHz

/* ==================== AW9523B GPIO Mapping ==================== */
// AW9523B manages GPIO pins for AW88298 control
#define AW9523B_P0_2_RST    0x02  // P0[2] -> AW88298 RST (active high)
#define AW9523B_P1_3_INT    0x13  // P1[3] -> AW88298 INT (input)

/* ==================== AW9523B Register Addresses ==================== */
#define AW9523B_INPUT_REG   0x00  // Input status
#define AW9523B_OUTPUT_REG  0x01  // Output control (P0)
#define AW9523B_OUTPUT_REG1 0x02  // Output control (P1)
#define AW9523B_CONFIG_REG  0x04  // Config P0 (0=OUT, 1=IN)
#define AW9523B_CONFIG_REG1 0x05  // Config P1 (0=OUT, 1=IN)

/* ==================== AW88298 Register Sequence ==================== */
struct aw88298_init_cmd {
  uint8_t reg;
  uint8_t value;
};

static const struct aw88298_init_cmd aw88298_init_seq[] = {
  // Register map (minimal init for audio playback)
  // Format: {register, value}
  {0x00, 0x01},  // SYSCTRL: Chip enable
  {0x02, 0x00},  // MODECTRL: Standard I2S mode
  {0x03, 0x00},  // CPCTRL: Default capacitance
  {0x04, 0x00},  // PWMCTRL: Default PWM control
  {0x05, 0x02},  // I2SCL: I2S format config (Philips, 16-bit)
  {0x06, 0x02},  // VOLCTRL: Volume control (default -70dB)
  {0xff, 0xff}   // Terminator
};

/* ==================== I2C Bus Port Selection ==================== */
#ifdef CONFIG_DAC_I2C_PORT
#define BOARD_I2C_PORT CONFIG_DAC_I2C_PORT
#else
#define BOARD_I2C_PORT 0
#endif

/* ==================== Global State ==================== */
static bool board_initialized = false;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t aw88298_dev_handle = NULL;
static i2c_master_dev_handle_t aw9523b_dev_handle = NULL;

/* ==================== Internal Helpers ==================== */

/**
 * Reset AW88298 via AW9523B RST pin (P0[2])
 * RST is active-high, so: RST=1 (normal), RST=0 (reset)
 */
static esp_err_t aw9523b_reset_aw88298(bool reset_assert) {
  esp_err_t err;
  
  // Read current P0 output state
  uint8_t p0_out = 0;
  err = board_i2c_read(aw9523b_dev_handle, AW9523B_OUTPUT_REG, &p0_out, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read AW9523B P0 output: %s", esp_err_to_name(err));
    return err;
  }
  
  // Modify P0[2] bit
  if (reset_assert) {
    p0_out &= ~(1 << 2);  // RST=0 (reset state)
  } else {
    p0_out |= (1 << 2);   // RST=1 (normal state)
  }
  
  // Write back
  err = board_i2c_write(aw9523b_dev_handle, AW9523B_OUTPUT_REG, &p0_out, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write AW9523B P0 output: %s", esp_err_to_name(err));
    return err;
  }
  
  return ESP_OK;
}

/**
 * Initialize AW9523B IO Expander
 * - Configure P0[2] as OUTPUT (AW88298 RST control)
 * - Configure P1[3] as INPUT (AW88298 INT feedback)
 */
static esp_err_t aw9523b_init(void) {
  esp_err_t err;
  
  // Add device to I2C bus
  err = board_i2c_add_device(i2c_bus_handle, AW9523B_ADDR, I2C_MASTER_SPEED,
                             &aw9523b_dev_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add AW9523B to I2C bus: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(TAG, "AW9523B detected @0x%02X", AW9523B_ADDR);
  
  // Configure P0[2] as OUTPUT for RST control
  uint8_t p0_config = 0xFB;  // P0[2]=0 (output), others=1 (input)
  err = board_i2c_write(aw9523b_dev_handle, AW9523B_CONFIG_REG, &p0_config, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure AW9523B P0: %s", esp_err_to_name(err));
    return err;
  }
  
  // Configure P1[3] as INPUT for INT feedback
  uint8_t p1_config = 0xFF;  // All as inputs
  err = board_i2c_write(aw9523b_dev_handle, AW9523B_CONFIG_REG1, &p1_config, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure AW9523B P1: %s", esp_err_to_name(err));
    return err;
  }
  
  // Initialize P0 output with RST=1 (normal operation)
  uint8_t p0_out = 0xFF;  // All outputs high initially
  err = board_i2c_write(aw9523b_dev_handle, AW9523B_OUTPUT_REG, &p0_out, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize AW9523B output: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(TAG, "AW9523B initialized (RST control ready)");
  return ESP_OK;
}

/**
 * Initialize AW88298 Audio Amplifier
 * 1. Assert RST via AW9523B
 * 2. Apply register initialization sequence
 * 3. Release RST
 */
static esp_err_t aw88298_init(void) {
  esp_err_t err;
  
  // Add device to I2C bus
  err = board_i2c_add_device(i2c_bus_handle, AW88298_ADDR, I2C_MASTER_SPEED,
                             &aw88298_dev_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add AW88298 to I2C bus: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(TAG, "AW88298 detected @0x%02X", AW88298_ADDR);
  
  // Reset sequence: assert RST, wait, release RST
  err = aw9523b_reset_aw88298(true);  // RST=0 (reset)
  if (err != ESP_OK) return err;
  
  vTaskDelay(pdMS_TO_TICKS(10));  // Hold RST low for 10ms
  
  err = aw9523b_reset_aw88298(false);  // RST=1 (normal)
  if (err != ESP_OK) return err;
  
  vTaskDelay(pdMS_TO_TICKS(20));  // Wait 20ms for device to stabilize
  
  // Apply initialization register sequence
  for (int i = 0; aw88298_init_seq[i].reg != 0xff; i++) {
    uint8_t reg = aw88298_init_seq[i].reg;
    uint8_t val = aw88298_init_seq[i].value;
    
    err = board_i2c_write(aw88298_dev_handle, reg, &val, 1);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to write AW88298 reg 0x%02X: %s",
               reg, esp_err_to_name(err));
      return err;
    }
  }
  
  ESP_LOGI(TAG, "AW88298 initialized successfully");
  return ESP_OK;
}

/* ==================== DAC Operations Implementation ==================== */

static esp_err_t dac_aw88298_init(void *i2c_bus) {
  esp_err_t err;
  
  if (i2c_bus == NULL) {
    ESP_LOGE(TAG, "No I2C bus handle provided");
    return ESP_ERR_INVALID_ARG;
  }
  
  i2c_bus_handle = (i2c_master_bus_handle_t)i2c_bus;
  
  // Initialize AW9523B IO Expander first (manages RST pin)
  err = aw9523b_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "AW9523B init failed");
    return err;
  }
  
  // Initialize AW88298 Audio Amplifier
  err = aw88298_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "AW88298 init failed");
    return err;
  }
  
  ESP_LOGI(TAG, "DAC initialization complete");
  return ESP_OK;
}

static esp_err_t dac_aw88298_deinit(void) {
  esp_err_t err = ESP_OK;
  
  if (aw88298_dev_handle) {
    err = board_i2c_remove_device(aw88298_dev_handle);
    aw88298_dev_handle = NULL;
  }
  
  if (aw9523b_dev_handle) {
    board_i2c_remove_device(aw9523b_dev_handle);
    aw9523b_dev_handle = NULL;
  }
  
  return err;
}

static void dac_aw88298_set_volume(float volume_db) {
  // Volume control via I2C register writes
  // AW88298 supports -70dB to 0dB range
  // For now, fixed at startup. Full implementation would map dB to register value.
  ESP_LOGD(TAG, "Volume set to %.1f dB", volume_db);
}

static void dac_aw88298_set_power_mode(dac_power_mode_t mode) {
  // Power mode control: ON, STANDBY, OFF
  // For now, always ON during operation
  ESP_LOGD(TAG, "Power mode: %d", mode);
}

static void dac_aw88298_enable_speaker(bool enable) {
  if (!aw88298_dev_handle) {
    ESP_LOGE(TAG, "AW88298 not initialized");
    return;
  }
  
  // Enable/disable speaker output via I2C register
  // Register 0x56 (PWACTRL) controls speaker enable
  uint8_t val = enable ? 0x00 : 0x10;
  esp_err_t err = board_i2c_write(aw88298_dev_handle, 0x56, &val, 1);
  
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Speaker %s", enable ? "enabled" : "disabled");
  } else {
    ESP_LOGE(TAG, "Failed to control speaker: %s", esp_err_to_name(err));
  }
}

static void dac_aw88298_enable_line_out(bool enable) {
  // CoreS3 has no line output, no-op
  (void)enable;
}

// DAC operations structure
static const dac_ops_t dac_aw88298_ops = {
  .init = dac_aw88298_init,
  .deinit = dac_aw88298_deinit,
  .set_volume = dac_aw88298_set_volume,
  .set_power_mode = dac_aw88298_set_power_mode,
  .enable_speaker = dac_aw88298_enable_speaker,
  .enable_line_out = dac_aw88298_enable_line_out,
};

/* ==================== Board-Level API ==================== */

esp_err_t iot_board_init(void) {
  if (board_initialized) {
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing M5Stack CoreS3 board");
  
  // Register DAC operations first
  dac_register(&dac_aw88298_ops);
  
  // Initialize I2C bus (board owns the bus lifetime)
  i2c_master_bus_config_t i2c_cfg = {
      .i2c_port = BOARD_I2C_PORT,
      .sda_io_num = BOARD_I2C_SDA,
      .scl_io_num = BOARD_I2C_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  
  esp_err_t err = i2c_new_master_bus(&i2c_cfg, &i2c_bus_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(TAG, "I2C bus initialized: SDA=%d, SCL=%d", 
           BOARD_I2C_SDA, BOARD_I2C_SCL);
  
  // Initialize the DAC
  err = dac_init(i2c_bus_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize DAC: %s", esp_err_to_name(err));
    i2c_del_master_bus(i2c_bus_handle);
    return err;
  }
  
  board_initialized = true;
  ESP_LOGI(TAG, "M5Stack CoreS3 board initialization complete");
  return ESP_OK;
}

esp_err_t iot_board_deinit(void) {
  if (!board_initialized) {
    return ESP_OK;
  }
  
  dac_deinit();
  
  // Clean up I2C devices
  if (aw88298_dev_handle) {
    board_i2c_remove_device(aw88298_dev_handle);
    aw88298_dev_handle = NULL;
  }
  
  if (aw9523b_dev_handle) {
    board_i2c_remove_device(aw9523b_dev_handle);
    aw9523b_dev_handle = NULL;
  }
  
  // Delete I2C bus
  if (i2c_bus_handle) {
    i2c_del_master_bus(i2c_bus_handle);
    i2c_bus_handle = NULL;
  }
  
  board_initialized = false;
  return ESP_OK;
}

bool iot_board_is_init(void) {
  return board_initialized;
}

board_res_handle_t iot_board_get_handle(int id) {
  switch (id) {
    case BOARD_I2C_DAC_ID:
      return (board_res_handle_t)i2c_bus_handle;
    default:
      return NULL;
  }
}

const char *iot_board_get_info(void) {
  return "M5Stack CoreS3 (ESP32-S3, AW88298 1W Speaker)";
}
