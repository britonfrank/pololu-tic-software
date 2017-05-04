#include "tic_internal.h"

static void write_settings_to_buffer(const tic_settings * settings, uint8_t * buf)
{
  assert(settings != NULL);
  assert(buf != NULL);

  buf[TIC_SETTING_CONTROL_MODE] =
    tic_settings_control_mode_get(settings);

  buf[TIC_SETTING_NEVER_SLEEP] =
    tic_settings_never_sleep_get(settings);

  buf[TIC_SETTING_DISABLE_SAFE_START] =
    tic_settings_disable_safe_start_get(settings);

  buf[TIC_SETTING_IGNORE_ERR_LINE_HIGH] =
    tic_settings_ignore_err_line_high_get(settings);

  {
    uint32_t baud_rate = tic_settings_baud_rate_get(settings);
    uint16_t brg = tic_baud_rate_to_brg(baud_rate);
    buf[TIC_SETTING_SERIAL_BAUD_RATE_GENERATOR + 0] = brg >> 0 & 0xFF;
    buf[TIC_SETTING_SERIAL_BAUD_RATE_GENERATOR + 1] = brg >> 8 & 0xFF;
  }

  buf[TIC_SETTING_SERIAL_DEVICE_NUMBER] =
    tic_settings_serial_device_number_get(settings);

  buf[TIC_SETTING_I2C_DEVICE_ADDRESS] =
    tic_settings_i2c_device_address_get(settings);

  {
    uint16_t command_timeout = tic_settings_command_timeout_get(settings);
    buf[TIC_SETTING_COMMAND_TIMEOUT + 0] = command_timeout >> 0 & 0xFF;
    buf[TIC_SETTING_COMMAND_TIMEOUT + 1] = command_timeout >> 8 & 0xFF;
  }

  buf[TIC_SETTING_SERIAL_CRC_ENABLED] =
    tic_settings_serial_crc_enabled_get(settings);

  {
    uint16_t low_vin_timeout = tic_settings_low_vin_timeout_get(settings);
    buf[TIC_SETTING_LOW_VIN_TIMEOUT + 0] = low_vin_timeout >> 0 & 0xFF;
    buf[TIC_SETTING_LOW_VIN_TIMEOUT + 1] = low_vin_timeout >> 8 & 0xFF;
  }

  {
    uint16_t voltage = tic_settings_low_vin_startup_voltage_get(settings);
    buf[TIC_SETTING_LOW_VIN_STARTUP_VOLTAGE + 0] = voltage >> 0 & 0xFF;
    buf[TIC_SETTING_LOW_VIN_STARTUP_VOLTAGE + 1] = voltage >> 8 & 0xFF;
  }

  {
    uint16_t voltage = tic_settings_low_vin_shutoff_voltage_get(settings);
    buf[TIC_SETTING_LOW_VIN_SHUTOFF_VOLTAGE + 0] = voltage >> 0 & 0xFF;
    buf[TIC_SETTING_LOW_VIN_SHUTOFF_VOLTAGE + 1] = voltage >> 8 & 0xFF;
  }

  {
    uint16_t offset = (uint16_t)tic_settings_vin_multiplier_offset_get(settings);
    buf[TIC_SETTING_VIN_MULTIPLIER_OFFSET + 0] = offset >> 0 & 0xFF;
    buf[TIC_SETTING_VIN_MULTIPLIER_OFFSET + 1] = offset >> 8 & 0xFF;
  }

  {
    uint16_t period = tic_settings_rc_max_pulse_period_get(settings);
    buf[TIC_SETTING_RC_MAX_PULSE_PERIOD + 0] = period >> 0 & 0xFF;
    buf[TIC_SETTING_RC_MAX_PULSE_PERIOD + 1] = period >> 8 & 0xFF;
  }

  {
    uint16_t timeout = tic_settings_rc_bad_signal_timeout_get(settings);
    buf[TIC_SETTING_RC_BAD_SIGNAL_TIMEOUT + 0] = timeout >> 0 & 0xFF;
    buf[TIC_SETTING_RC_BAD_SIGNAL_TIMEOUT + 1] = timeout >> 8 & 0xFF;
  }

  {
    uint16_t pulses = tic_settings_rc_consecutive_good_pulses_get(settings);
    buf[TIC_SETTING_RC_CONSECUTIVE_GOOD_PULSES + 0] = pulses >> 0 & 0xFF;
    buf[TIC_SETTING_RC_CONSECUTIVE_GOOD_PULSES + 1] = pulses >> 8 & 0xFF;
  }

  buf[TIC_SETTING_INPUT_PLAY] = tic_settings_input_play_get(settings);

  {
    uint16_t input_error_min = tic_settings_input_error_min_get(settings);
    buf[TIC_SETTING_INPUT_ERROR_MIN + 0] = input_error_min >> 0 & 0xFF;
    buf[TIC_SETTING_INPUT_ERROR_MIN + 1] = input_error_min >> 8 & 0xFF;
  }

  {
    uint16_t input_error_max = tic_settings_input_error_max_get(settings);
    buf[TIC_SETTING_INPUT_ERROR_MAX + 0] = input_error_max >> 0 & 0xFF;
    buf[TIC_SETTING_INPUT_ERROR_MAX + 1] = input_error_max >> 8 & 0xFF;
  }

  buf[TIC_SETTING_INPUT_SCALING_DEGREE] =
    tic_settings_input_scaling_degree_get(settings);

  buf[TIC_SETTING_INPUT_INVERT] =
    tic_settings_input_invert_get(settings);

  {
    uint16_t input_min = tic_settings_input_min_get(settings);
    buf[TIC_SETTING_INPUT_MIN + 0] = input_min >> 0 & 0xFF;
    buf[TIC_SETTING_INPUT_MIN + 1] = input_min >> 8 & 0xFF;
  }

  {
    uint16_t input_neutral_min = tic_settings_input_neutral_min_get(settings);
    buf[TIC_SETTING_INPUT_NEUTRAL_MIN + 0] = input_neutral_min >> 0 & 0xFF;
    buf[TIC_SETTING_INPUT_NEUTRAL_MIN + 1] = input_neutral_min >> 8 & 0xFF;
  }

  {
    uint16_t input_neutral_max = tic_settings_input_neutral_max_get(settings);
    buf[TIC_SETTING_INPUT_NEUTRAL_MAX + 0] = input_neutral_max >> 0 & 0xFF;
    buf[TIC_SETTING_INPUT_NEUTRAL_MAX + 1] = input_neutral_max >> 8 & 0xFF;
  }

  {
    uint16_t input_max = tic_settings_input_max_get(settings);
    buf[TIC_SETTING_INPUT_MAX + 0] = input_max >> 0 & 0xFF;
    buf[TIC_SETTING_INPUT_MAX + 1] = input_max >> 8 & 0xFF;
  }

  {
    uint32_t output = (uint32_t)tic_settings_output_min_get(settings);
    buf[TIC_SETTING_OUTPUT_MIN + 0] = output >> 0 & 0xFF;
    buf[TIC_SETTING_OUTPUT_MIN + 1] = output >> 8 & 0xFF;
    buf[TIC_SETTING_OUTPUT_MIN + 2] = output >> 16 & 0xFF;
    buf[TIC_SETTING_OUTPUT_MIN + 3] = output >> 24 & 0xFF;
  }

  {
    uint32_t output = (uint32_t)tic_settings_output_neutral_get(settings);
    buf[TIC_SETTING_OUTPUT_NEUTRAL + 0] = output >> 0 & 0xFF;
    buf[TIC_SETTING_OUTPUT_NEUTRAL + 1] = output >> 8 & 0xFF;
    buf[TIC_SETTING_OUTPUT_NEUTRAL + 2] = output >> 16 & 0xFF;
    buf[TIC_SETTING_OUTPUT_NEUTRAL + 3] = output >> 24 & 0xFF;
  }

  {
    uint32_t output = (uint32_t)tic_settings_output_max_get(settings);
    buf[TIC_SETTING_OUTPUT_MAX + 0] = output >> 0 & 0xFF;
    buf[TIC_SETTING_OUTPUT_MAX + 1] = output >> 8 & 0xFF;
    buf[TIC_SETTING_OUTPUT_MAX + 2] = output >> 16 & 0xFF;
    buf[TIC_SETTING_OUTPUT_MAX + 3] = output >> 24 & 0xFF;
  }

  buf[TIC_SETTING_ENCODER_PRESCALER] =
    tic_settings_encoder_prescaler_get(settings);

  {
    uint32_t postscaler = tic_settings_encoder_postscaler_get(settings);
    buf[TIC_SETTING_ENCODER_POSTSCALER + 0] = postscaler >> 0 & 0xFF;
    buf[TIC_SETTING_ENCODER_POSTSCALER + 1] = postscaler >> 8 & 0xFF;
    buf[TIC_SETTING_ENCODER_POSTSCALER + 2] = postscaler >> 16 & 0xFF;
    buf[TIC_SETTING_ENCODER_POSTSCALER + 3] = postscaler >> 24 & 0xFF;
  }

  buf[TIC_SETTING_SCL_CONFIG] = tic_settings_scl_config_get(settings);

  buf[TIC_SETTING_SDA_CONFIG] = tic_settings_sda_config_get(settings);

  buf[TIC_SETTING_TX_CONFIG] = tic_settings_tx_config_get(settings);

  buf[TIC_SETTING_RX_CONFIG] = tic_settings_rx_config_get(settings);

  buf[TIC_SETTING_RC_CONFIG] = tic_settings_rc_config_get(settings);

  buf[TIC_SETTING_MICROSTEPPING_MODE] =
    tic_settings_microstepping_mode_get(settings);

  buf[TIC_SETTING_DECAY_MODE] = tic_settings_decay_mode_get(settings);

  {
    uint32_t speed = tic_settings_speed_min_get(settings);
    buf[TIC_SETTING_SPEED_MIN + 0] = speed >> 0 & 0xFF;
    buf[TIC_SETTING_SPEED_MIN + 1] = speed >> 1 & 0xFF;
    buf[TIC_SETTING_SPEED_MIN + 2] = speed >> 2 & 0xFF;
    buf[TIC_SETTING_SPEED_MIN + 3] = speed >> 3 & 0xFF;
  }

  {
    uint32_t speed = tic_settings_speed_max_get(settings);
    buf[TIC_SETTING_SPEED_MAX + 0] = speed >> 0 & 0xFF;
    buf[TIC_SETTING_SPEED_MAX + 1] = speed >> 1 & 0xFF;
    buf[TIC_SETTING_SPEED_MAX + 2] = speed >> 2 & 0xFF;
    buf[TIC_SETTING_SPEED_MAX + 3] = speed >> 3 & 0xFF;
  }

  {
    uint32_t decel = tic_settings_decel_max_get(settings);
    buf[TIC_SETTING_DECEL_MAX + 0] = decel >> 0 & 0xFF;
    buf[TIC_SETTING_DECEL_MAX + 1] = decel >> 1 & 0xFF;
    buf[TIC_SETTING_DECEL_MAX + 2] = decel >> 2 & 0xFF;
    buf[TIC_SETTING_DECEL_MAX + 3] = decel >> 3 & 0xFF;
  }

  {
    uint32_t accel = tic_settings_accel_max_get(settings);
    buf[TIC_SETTING_ACCEL_MAX + 0] = accel >> 0 & 0xFF;
    buf[TIC_SETTING_ACCEL_MAX + 1] = accel >> 1 & 0xFF;
    buf[TIC_SETTING_ACCEL_MAX + 2] = accel >> 2 & 0xFF;
    buf[TIC_SETTING_ACCEL_MAX + 3] = accel >> 3 & 0xFF;
  }

  {
    uint32_t decel = tic_settings_decel_max_during_error_get(settings);
    buf[TIC_SETTING_DECEL_MAX_DURING_ERROR + 0] = decel >> 0 & 0xFF;
    buf[TIC_SETTING_DECEL_MAX_DURING_ERROR + 1] = decel >> 1 & 0xFF;
    buf[TIC_SETTING_DECEL_MAX_DURING_ERROR + 2] = decel >> 2 & 0xFF;
    buf[TIC_SETTING_DECEL_MAX_DURING_ERROR + 3] = decel >> 3 & 0xFF;
  }
}

static tic_error * write_setting_byte(tic_handle * handle,
  uint8_t address, uint8_t byte)
{
  assert(handle != NULL);

  libusbp_generic_handle * usb_handle = tic_handle_get_usb_handle(handle);

  tic_error * error = tic_usb_error(libusbp_control_transfer(usb_handle,
      0xC0, TIC_CMD_WRITE_SETTING, byte, address, NULL, 0, NULL));
  return error;
}

tic_error * tic_set_settings(tic_handle * handle, const tic_settings * settings)
{
  if (handle == NULL)
  {
    return tic_error_create("Handle is NULL.");
  }

  if (settings == NULL)
  {
    return tic_error_create("Settings object is NULL.");
  }

  tic_error * error = NULL;

  tic_settings * fixed_settings = NULL;

  // Copy the settings so we can fix them without modifying the input ones,
  // which would be surprising to the caller.
  if (error == NULL)
  {
    error = tic_settings_copy(settings, &fixed_settings);
  }

  // Fix the settings silently to make sure we don't apply invalid settings to
  // the device.  A good app will call tic_settings_fix on its own before
  // calling this function, so there should be nothing to fix here.
  if (error == NULL)
  {
    error = tic_settings_fix(fixed_settings, NULL);
  }

  // Construct a buffer holding the bytes we want to write.
  uint8_t buf[TIC_HIGHEST_SETTING_ADDRESS + 1];
  memset(buf, 0, sizeof(buf));
  write_settings_to_buffer(fixed_settings, buf);

  // Write the bytes to the device.
  for (uint8_t i = 1; i < sizeof(buf) && error == NULL; i++)
  {
    error = write_setting_byte(handle, i, buf[i]);
  }

  tic_settings_free(fixed_settings);

  return error;
}