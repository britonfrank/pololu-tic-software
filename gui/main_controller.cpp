#include "main_controller.h"
#include "main_window.h"
#include <file_util.h>

#include <cassert>
#include <cmath>

// This is how often we fetch the variables from the device.
static const uint32_t UPDATE_INTERVAL_MS = 50;

// Only update the device list once per second to save CPU time.
static const uint32_t UPDATE_DEVICE_LIST_DIVIDER = 20;

void main_controller::set_window(main_window * window)
{
  this->window = window;
}

void main_controller::start()
{
  assert(!connected());

  // Start the update timer so that update() will be called regularly.
  window->set_update_timer_interval(UPDATE_INTERVAL_MS);
  window->start_update_timer();

  handle_model_changed();
}

// Returns the device that matches the specified OS ID from the list, or a null
// device if none match.
static tic::device device_with_os_id(
  std::vector<tic::device> const & device_list,
  std::string const & id)
{
  for (tic::device const & candidate : device_list)
  {
    if (candidate.get_os_id() == id)
    {
      return candidate;
    }
  }
  return tic::device(); // null device
}

void main_controller::connect_device_with_os_id(std::string const & id)
{
  // TODO: deal with id being invalid
  connect_device(device_with_os_id(device_list, id));
}

bool main_controller::disconnect_device()
{
  if (!connected()) { return true; }

  if (settings_modified)
  {
    std::string question =
      "The settings you changed have not been applied to the device.  "
      "If you disconnect from the device now, those changes will be lost.  "
      "Are you sure you want to disconnect?";
    if (!window->confirm(question))
    {
      return false;
    }
  }

  really_disconnect();
  disconnected_by_user = true;
  connection_error = false;
  handle_model_changed();
  return true;
}

void main_controller::connect_device(tic::device const & device)
{
  assert(device);

  try
  {
    // Close the old handle in case one is already open.
    device_handle.close();

    connection_error = false;
    disconnected_by_user = false;

    // Open a handle to the specified device.
    device_handle = tic::handle(device);
  }
  catch (std::exception const & e)
  {
    set_connection_error("Failed to connect to device.");
    show_exception(e, "There was an error connecting to the device.");
    handle_model_changed();
    return;
  }

  try
  {
    settings = device_handle.get_settings();
    handle_settings_applied();
  }
  catch (std::exception const & e)
  {
    show_exception(e, "There was an error loading settings from the device.");
  }

  try
  {
    // Reset command timeout BEFORE loading variables for the first time so we
    // only count one occurrence of the command timeout error. (Otherwise, the
    // Tic would again set the command_timeout bit in errors_occurred after we
    // load the variables but before we reset the command timeout.)
    device_handle.reset_command_timeout();
    reload_variables();
  }
  catch (std::exception const & e)
  {
    show_exception(e, "There was an error getting the status of the device.");
  }

  handle_model_changed();
}

void main_controller::disconnect_device_by_error(std::string const & error_message)
{
  really_disconnect();
  disconnected_by_user = false;
  set_connection_error(error_message);
}

void main_controller::really_disconnect()
{
  device_handle.close();
  settings_modified = false;
}

void main_controller::set_connection_error(std::string const & error_message)
{
  connection_error = true;
  connection_error_message = error_message;
}

void main_controller::reload_settings(bool ask)
{
  if (!connected()) { return; }

  std::string question = "Are you sure you want to reload settings from the "
    "device and discard your recent changes?";
  if (ask && !window->confirm(question))
  {
    return;
  }

  try
  {
    settings = device_handle.get_settings();
    handle_settings_applied();
    settings_modified = false;
  }
  catch (std::exception const & e)
  {
    settings_modified = true;
    show_exception(e, "There was an error loading the settings from the device.");
  }
  handle_settings_changed();
}

void main_controller::restore_default_settings()
{
  if (!connected()) { return; }

  std::string question = "This will reset all of your device's settings "
    "back to their default values.  "
    "You will lose your custom settings.  "
    "Are you sure you want to continue?";
  if (!window->confirm(question))
  {
    return;
  }

  bool restore_success = false;
  try
  {
    device_handle.restore_defaults();
    restore_success = true;
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }

  // This takes care of reloading the settings and telling the view to update.
  reload_settings(false);

  if (restore_success)
  {
    window->show_info_message(
      "Your device's settings have been reset to their default values.");
  }
}

void main_controller::upgrade_firmware()
{
  if (connected())
  {
    std::string question =
      "This action will restart the device in bootloader mode, which "
      "is used for firmware upgrades.  The device will disconnect "
      "and reappear to your system as a new device.\n\n"
      "Are you sure you want to proceed?";
    if (!window->confirm(question))
    {
      return;
    }

    try
    {
      device_handle.start_bootloader();
    }
    catch (std::exception const & e)
    {
      show_exception(e);
    }

    really_disconnect();
    disconnected_by_user = true;
    connection_error = false;
    handle_model_changed();
  }

  window->open_bootloader_window();
}

// Returns true if the device list includes the specified device.
static bool device_list_includes(
  std::vector<tic::device> const & device_list,
  tic::device const & device)
{
  return device_with_os_id(device_list, device.get_os_id());
}

void main_controller::update()
{
  // This is called regularly by the view when it is time to check for
  // updates to the state of USB devices.  This runs on the same thread as
  // everything else, so we should be careful not to do anything too slow
  // here.  If the user tries to use the UI at all while this function is
  // running, the UI cannot respond until this function returns.

  bool successfully_updated_list = false;
  if (--update_device_list_counter == 0)
  {
    update_device_list_counter = UPDATE_DEVICE_LIST_DIVIDER;

    successfully_updated_list = update_device_list();
    if (successfully_updated_list && device_list_changed)
    {
      window->set_device_list_contents(device_list);
      if (connected())
      {
        window->set_device_list_selected(device_handle.get_device());
      }
      else
      {
        window->set_device_list_selected(tic::device()); // show "Not connected"
      }
    }
  }

  if (connected())
  {
    // First, see if the device we are connected to is still available.
    // Note: It would be nice if the libusbp::generic_handle class had a
    // function that tests if the actual handle we are using is still valid.
    // This would be better for tricky cases like if someone unplugs and
    // plugs the same device in very fast.
    bool device_still_present = device_list_includes(
      device_list, device_handle.get_device());

    if (device_still_present)
    {
      // Reload the variables from the device.
      try
      {
        // Reset command timeout AFTER reloading the variables so we can
        // indicate an active error if the command timeout interval is shorter
        // than the interval between calls to update().
        reload_variables();
        device_handle.reset_command_timeout();
      }
      catch (std::exception const & e)
      {
        // Ignore the exception.  The model provides other ways to tell that
        // the variable update failed, and the exact message is probably
        // not that useful since it is probably just a generic problem with
        // the USB connection.
      }
      handle_variables_changed();
    }
    else
    {
      // The device is gone.
      disconnect_device_by_error("The connection to the device was lost.");
      handle_model_changed();
    }
  }
  else
  {
    // We are not connected, so consider auto-connecting to a device.

    if (connection_error)
    {
      // There is an error related to a previous connection or connection
      // attempt, so don't automatically reconnect.  That would be
      // confusing, because the user might be looking away and not notice
      // that the connection was lost and then regained, or they could be
      // trying to read the error message.
    }
    else if (disconnected_by_user)
    {
      // The user explicitly disconnected the last connection, so don't
      // automatically reconnect.
    }
    else if (successfully_updated_list && (device_list.size() == 1))
    {
      // Automatically connect if there is only one device and we were not
      // recently disconnected from a device.
      connect_device(device_list.at(0));
    }
  }
}

bool main_controller::exit()
{
  if (connected() && settings_modified)
  {
    std::string question =
      "The settings you changed have not been applied to the device.  "
      "If you exit now, those changes will be lost.  "
      "Are you sure you want to exit?";
    return window->confirm(question);
  }
  else
  {
    return true;
  }
}

static bool device_lists_different(std::vector<tic::device> const & list1,
  std::vector<tic::device> const & list2)
{
  if (list1.size() != list2.size())
  {
    return true;
  }
  else
  {
    for (int i = 0; i < list1.size(); i++)
    {
      if (list1.at(i).get_os_id() != list2.at(i).get_os_id())
      {
        return true;
      }
    }
    return false;
  }
}

bool main_controller::update_device_list()
{
  try
  {
    std::vector<tic::device> new_device_list = tic::list_connected_devices();
    if (device_lists_different(device_list, new_device_list))
    {
      device_list_changed = true;
    }
    else
    {
      device_list_changed = false;
    }
    device_list = new_device_list;
    return true;
  }
  catch (std::exception const & e)
  {
    set_connection_error("Failed to get the list of devices.");
    show_exception(e, "There was an error getting the list of devices.");
    return false;
  }
}

void main_controller::show_exception(std::exception const & e,
    std::string const & context)
{
    std::string message;
    if (context.size() > 0)
    {
      message += context;
      message += "  ";
    }
    message += e.what();
    window->show_error_message(message);
}

void main_controller::handle_model_changed()
{
  handle_device_changed();
  handle_variables_changed();
  handle_settings_changed();
}

void main_controller::handle_device_changed()
{
  if (connected())
  {
    tic::device const & device = device_handle.get_device();
    window->set_device_name(device.get_name(), true);
    window->set_serial_number(device.get_serial_number());
    window->set_firmware_version(device_handle.get_firmware_version_string());
    window->set_device_reset(
      tic_look_up_device_reset_name_ui(variables.get_device_reset()));

    window->set_device_list_selected(device);
    window->set_connection_status("", false);

    window->reset_error_counts();

    initialize_manual_target();
  }
  else
  {
    std::string value = "N/A";
    window->set_device_name(value, false);
    window->set_serial_number(value);
    window->set_firmware_version(value);

    window->set_device_list_selected(tic::device()); // show "Not connected"

    if (connection_error)
    {
      window->set_connection_status(connection_error_message, true);
    }
    else
    {
      window->set_connection_status("", false);
    }
  }

  window->set_disconnect_enabled(connected());
  window->set_open_save_settings_enabled(connected());
  window->set_reload_settings_enabled(connected());
  window->set_restore_defaults_enabled(connected());
  window->set_tab_pages_enabled(connected());
}

void main_controller::initialize_manual_target()
{
  if (control_mode_is_serial(cached_settings) &&
    (variables.get_input_state() == TIC_INPUT_STATE_POSITION))
  {
    window->set_manual_target_position_mode();
    window->set_displayed_manual_target(variables.get_input_after_scaling());
  }
  else if (control_mode_is_serial(cached_settings) &&
    (variables.get_input_state() == TIC_INPUT_STATE_VELOCITY))
  {
    window->set_manual_target_velocity_mode();
    window->set_displayed_manual_target(variables.get_input_after_scaling());
  }
  else
  {
    window->set_manual_target_position_mode();
    window->set_displayed_manual_target(0);
  }
}

void main_controller::handle_variables_changed()
{
  window->set_up_time(variables.get_up_time());

  window->set_encoder_position(variables.get_encoder_position());
  window->set_input_state(
    tic_look_up_input_state_name_ui(variables.get_input_state()),
    variables.get_input_state());
  window->set_input_after_averaging(variables.get_input_after_averaging());
  window->set_input_after_hysteresis(variables.get_input_after_hysteresis());
  if (cached_settings)
  {
    window->set_input_before_scaling(
      variables.get_input_before_scaling(cached_settings),
      tic_settings_get_control_mode(settings.get_pointer()));
  }
  window->set_input_after_scaling(variables.get_input_after_scaling());

  window->set_vin_voltage(variables.get_vin_voltage());
  window->set_energized(variables.get_energized());
  window->set_operation_state(
    tic_look_up_operation_state_name_ui(variables.get_operation_state()));

  int32_t target_position = variables.get_target_position();
  int32_t target_velocity = variables.get_target_velocity();
  int32_t current_position = variables.get_current_position();
  int32_t current_velocity = variables.get_current_velocity();

  bool target_valid = true;
  if (variables.get_planning_mode() == TIC_PLANNING_MODE_TARGET_POSITION)
  {
    window->set_target_position(target_position);
  }
  else if (variables.get_planning_mode() == TIC_PLANNING_MODE_TARGET_VELOCITY)
  {
    window->set_target_velocity(target_velocity);
  }
  else
  {
    window->set_target_none();
    target_valid = false;
  }

  window->set_manual_target_ball_position(current_position,
    target_valid && (current_position == target_position));
  window->set_manual_target_ball_velocity(current_velocity,
    target_valid && (current_velocity == target_velocity));

  window->set_current_position(current_position);
  window->set_position_uncertain(variables.get_position_uncertain());
  window->set_current_velocity(current_velocity);

  uint16_t error_status = variables.get_error_status();

  window->set_error_status(error_status);
  window->increment_errors_occurred(variables.get_errors_occurred());

  // We could enable the de-energize button only when the motor is not
  // intentionally de-energized, but instead we enable it all the time (when
  // connected to a device) so that people aren't nervous to see it disabled.
  window->set_deenergize_button_enabled(connected());

  uint16_t resumable_errors = 1 << TIC_ERROR_INTENTIONALLY_DEENERGIZED;
  bool resume_button_enabled, prompt_to_resume;
  if (control_mode_is_serial(cached_settings))
  {
    resumable_errors |= 1 << TIC_ERROR_SERIAL_ERROR;
    resumable_errors |= 1 << TIC_ERROR_COMMAND_TIMEOUT;
    resumable_errors |= 1 << TIC_ERROR_SAFE_START_VIOLATION;

    // Enable the resume button and prompt to resume if and only if we are
    // connected, there are errors, and there are no errors that the resume
    // button can't clear.
    resume_button_enabled = connected() && error_status &&
      !(error_status & ~resumable_errors);
    prompt_to_resume = resume_button_enabled;
  }
  else
  {
    // Enable the resume button if we are connected and the motor is
    // intentionally de-energized. This means that the resume button is simpler
    // in non-serial modes: it isn't guaranteed to clear all the errors, but it
    // exactly reverses the action of the de-energize button.
    resume_button_enabled = connected() && (error_status & resumable_errors);
    // Prompt to resume if and only if we are connected, there are errors, and
    // there are no errors that the resume button can't clear.
    prompt_to_resume = connected() && error_status &&
      !(error_status & ~resumable_errors);
  }
  window->set_resume_button_enabled(resume_button_enabled);
  update_motor_status_message(prompt_to_resume);
}

void main_controller::update_motor_status_message(bool prompt_to_resume)
{
  std::string msg;
  bool stopped = true;
  uint16_t error_status = variables.get_error_status();

  if (!connected())
  {
    msg = "";
  }
  else if (!error_status)
  {
    msg = "Driving";
    stopped = false;
  }
  else if (error_status & (1 << TIC_ERROR_MOTOR_DRIVER_ERROR))
  {
    msg = "Motor de-energized because of motor driver error.";
  }
  else if (error_status & (1 << TIC_ERROR_LOW_VIN))
  {
    msg = "Motor de-energized because VIN is too low.";
  }
  else if (error_status & (1 << TIC_ERROR_INTENTIONALLY_DEENERGIZED))
  {
    msg = "Motor intentionally de-energized.";
  }
  else
  {
    msg = "Motor ";

    if (!variables.get_energized())
    {
      msg += "de-energized ";
    }
    else if (variables.get_current_velocity() == 0)
    {
      msg += "holding ";
    }
    else if (variables.get_planning_mode() == TIC_PLANNING_MODE_TARGET_VELOCITY)
    {
      msg += "decelerating ";
    }
    else if (variables.get_planning_mode() == TIC_PLANNING_MODE_TARGET_POSITION)
    {
      msg += "moving to error position ";
    }

    if (error_status & (1 << TIC_ERROR_KILL_SWITCH))
    {
      msg += "because kill switch is active.";
    }
    else if (error_status & (1 << TIC_ERROR_REQUIRED_INPUT_INVALID))
    {
      msg += "because required input is invalid.";
    }
    else if (error_status & (1 << TIC_ERROR_SERIAL_ERROR))
    {
      msg += "because of serial error.";
    }
    else if (error_status & (1 << TIC_ERROR_COMMAND_TIMEOUT))
    {
      msg += "because of command timeout.";
    }
    else if (error_status & (1 << TIC_ERROR_SAFE_START_VIOLATION))
    {
      msg += "because of safe start violation.";

      uint8_t control_mode = tic_settings_get_control_mode(
        cached_settings.get_pointer());
      switch (control_mode)
      {
      case TIC_CONTROL_MODE_RC_SPEED:
      case TIC_CONTROL_MODE_ANALOG_SPEED:
      case TIC_CONTROL_MODE_ENCODER_SPEED:
        msg += "  Center the input.";
      }
    }
    else if (error_status & (1 << TIC_ERROR_ERR_LINE_HIGH))
    {
      msg += "because ERR line is high.";
    }
  }

  if (prompt_to_resume)
  {
    msg += "  Press Resume to start.";
  }

  window->set_motor_status_message(msg, stopped);
}

void main_controller::handle_settings_changed()
{
  // [all-settings]

  window->set_control_mode(tic_settings_get_control_mode(settings.get_pointer()));
  window->set_serial_baud_rate(tic_settings_get_serial_baud_rate(settings.get_pointer()));
  window->set_serial_device_number(tic_settings_get_serial_device_number(settings.get_pointer()));
  window->set_serial_crc_enabled(tic_settings_get_serial_crc_enabled(settings.get_pointer()));
  window->set_serial_response_delay(tic_settings_get_serial_response_delay(settings.get_pointer()));
  window->set_command_timeout(tic_settings_get_command_timeout(settings.get_pointer()));

  window->set_encoder_prescaler(tic_settings_get_encoder_prescaler(settings.get_pointer()));
  window->set_encoder_postscaler(tic_settings_get_encoder_postscaler(settings.get_pointer()));
  window->set_encoder_unlimited(tic_settings_get_encoder_unlimited(settings.get_pointer()));

  window->set_input_averaging_enabled(tic_settings_get_input_averaging_enabled(settings.get_pointer()));
  window->set_input_hysteresis(tic_settings_get_input_hysteresis(settings.get_pointer()));

  window->set_input_invert(tic_settings_get_input_invert(settings.get_pointer()));
  window->set_input_min(tic_settings_get_input_min(settings.get_pointer()));
  window->set_input_neutral_min(tic_settings_get_input_neutral_min(settings.get_pointer()));
  window->set_input_neutral_max(tic_settings_get_input_neutral_max(settings.get_pointer()));
  window->set_input_max(tic_settings_get_input_max(settings.get_pointer()));
  window->set_output_min(tic_settings_get_output_min(settings.get_pointer()));
  window->set_output_max(tic_settings_get_output_max(settings.get_pointer()));
  window->set_input_scaling_degree(tic_settings_get_input_scaling_degree(settings.get_pointer()));

  window->set_invert_motor_direction(tic_settings_get_invert_motor_direction(settings.get_pointer()));
  window->set_speed_max(tic_settings_get_max_speed(settings.get_pointer()));
  window->set_starting_speed(tic_settings_get_starting_speed(settings.get_pointer()));
  window->set_accel_max(tic_settings_get_max_accel(settings.get_pointer()));
  window->set_decel_max(tic_settings_get_max_decel(settings.get_pointer()));
  window->set_step_mode(tic_settings_get_step_mode(settings.get_pointer()));
  window->set_current_limit(tic_settings_get_current_limit(settings.get_pointer()));
  window->set_decay_mode(tic_settings_get_decay_mode(settings.get_pointer()));

  window->set_soft_error_response(tic_settings_get_soft_error_response(settings.get_pointer()));
  window->set_soft_error_position(tic_settings_get_soft_error_position(settings.get_pointer()));
  window->set_current_limit_during_error(tic_settings_get_current_limit_during_error(settings.get_pointer()));

  window->set_disable_safe_start(tic_settings_get_disable_safe_start(settings.get_pointer()));
  window->set_ignore_err_line_high(tic_settings_get_ignore_err_line_high(settings.get_pointer()));
  window->set_auto_clear_driver_error(tic_settings_get_auto_clear_driver_error(settings.get_pointer()));
  window->set_never_sleep(tic_settings_get_never_sleep(settings.get_pointer()));
  window->set_vin_calibration(tic_settings_get_vin_calibration(settings.get_pointer()));

  for (int i = 0; i < 5; i++)
  {
    uint8_t func = tic_settings_get_pin_func(settings.get_pointer(), i);
    bool pullup = tic_settings_get_pin_pullup(settings.get_pointer(), i);
    bool polarity = tic_settings_get_pin_polarity(settings.get_pointer(), i);
    bool analog = tic_settings_get_pin_analog(settings.get_pointer(), i);

    bool enabled = func != TIC_PIN_FUNC_DEFAULT;
    bool pullup_enabled = enabled && func != TIC_PIN_FUNC_POT_POWER;
    bool polarity_enabled = func == TIC_PIN_FUNC_KILL_SWITCH;
    bool analog_enabled = enabled;

    window->set_pin_func(i, func);
    window->set_pin_pullup(i, pullup, pullup_enabled);
    window->set_pin_polarity(i, polarity, polarity_enabled);
    window->set_pin_analog(i, analog, analog_enabled);
  }

  window->set_apply_settings_enabled(connected() && settings_modified);
}

void main_controller::handle_settings_applied()
{
  window->set_manual_target_enabled(control_mode_is_serial(settings));

  // this must be last so the preceding code can compare old and new settings
  cached_settings = settings;
}

void main_controller::handle_control_mode_input(uint8_t control_mode)
{
  if (!connected()) { return; }
  tic_settings_set_control_mode(settings.get_pointer(), control_mode);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_serial_baud_rate_input(uint32_t serial_baud_rate)
{
  if (!connected()) { return; }
  tic_settings_set_serial_baud_rate(settings.get_pointer(), serial_baud_rate);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_serial_baud_rate_input_finished()
{
  if (!connected()) { return; }
  uint32_t serial_baud_rate = tic_settings_get_serial_baud_rate(settings.get_pointer());
  serial_baud_rate = tic_settings_achievable_serial_baud_rate(
    settings.get_pointer(), serial_baud_rate);
  tic_settings_set_serial_baud_rate(settings.get_pointer(), serial_baud_rate);
  handle_settings_changed();
}

void main_controller::handle_serial_device_number_input(uint8_t serial_device_number)
{
  if (!connected()) { return; }
  tic_settings_set_serial_device_number(settings.get_pointer(), serial_device_number);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_serial_crc_enabled_input(bool serial_crc_enabled)
{
  if (!connected()) { return; }
  tic_settings_set_serial_crc_enabled(settings.get_pointer(), serial_crc_enabled);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_serial_response_delay_input(uint8_t delay)
{
  if (!connected()) { return; }
  tic_settings_set_serial_response_delay(settings.get_pointer(), delay);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_command_timeout_input(uint16_t command_timeout)
{
  if (!connected()) { return; }
  tic_settings_set_command_timeout(settings.get_pointer(), command_timeout);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_encoder_prescaler_input(uint32_t encoder_prescaler)
{
  if (!connected()) { return; }
  tic_settings_set_encoder_prescaler(settings.get_pointer(), encoder_prescaler);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_encoder_postscaler_input(uint32_t encoder_postscaler)
{
  if (!connected()) { return; }
  tic_settings_set_encoder_postscaler(settings.get_pointer(), encoder_postscaler);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_encoder_unlimited_input(bool encoder_unlimited)
{
  if (!connected()) { return; }
  tic_settings_set_encoder_unlimited(settings.get_pointer(), encoder_unlimited);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_averaging_enabled_input(bool input_averaging_enabled)
{
  if (!connected()) { return; }
  tic_settings_set_input_averaging_enabled(settings.get_pointer(), input_averaging_enabled);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_hysteresis_input(uint16_t input_hysteresis)
{
  if (!connected()) { return; }
  tic_settings_set_input_hysteresis(settings.get_pointer(), input_hysteresis);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_invert_input(bool input_invert)
{
  if (!connected()) { return; }
  tic_settings_set_input_invert(settings.get_pointer(), input_invert);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_min_input(uint16_t input_min)
{
  if (!connected()) { return; }
  tic_settings_set_input_min(settings.get_pointer(), input_min);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_neutral_min_input(uint16_t input_neutral_min)
{
  if (!connected()) { return; }
  tic_settings_set_input_neutral_min(settings.get_pointer(), input_neutral_min);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_neutral_max_input(uint16_t input_neutral_max)
{
  if (!connected()) { return; }
  tic_settings_set_input_neutral_max(settings.get_pointer(), input_neutral_max);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_max_input(uint16_t input_max)
{
  if (!connected()) { return; }
  tic_settings_set_input_max(settings.get_pointer(), input_max);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_output_min_input(int32_t output_min)
{
  if (!connected()) { return; }
  tic_settings_set_output_min(settings.get_pointer(), output_min);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_output_max_input(int32_t output_max)
{
  if (!connected()) { return; }
  tic_settings_set_output_max(settings.get_pointer(), output_max);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_input_scaling_degree_input(uint8_t input_scaling_degree)
{
  if (!connected()) { return; }
  tic_settings_set_input_scaling_degree(settings.get_pointer(), input_scaling_degree);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_invert_motor_direction_input(bool invert_motor_direction)
{
  if (!connected()) { return; }
  tic_settings_set_invert_motor_direction(settings.get_pointer(), invert_motor_direction);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_speed_max_input(uint32_t speed_max)
{
  if (!connected()) { return; }
  tic_settings_set_max_speed(settings.get_pointer(), speed_max);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_starting_speed_input(uint32_t starting_speed)
{
  if (!connected()) { return; }
  tic_settings_set_starting_speed(settings.get_pointer(), starting_speed);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_accel_max_input(uint32_t accel_max)
{
  if (!connected()) { return; }
  tic_settings_set_max_accel(settings.get_pointer(), accel_max);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_decel_max_input(uint32_t decel_max)
{
  if (!connected()) { return; }
  tic_settings_set_max_decel(settings.get_pointer(), decel_max);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_step_mode_input(uint8_t step_mode)
{
  if (!connected()) { return; }
  tic_settings_set_step_mode(settings.get_pointer(), step_mode);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_current_limit_input(uint32_t current_limit)
{
  if (!connected()) { return; }
  tic_settings_set_current_limit(settings.get_pointer(), current_limit);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_current_limit_input_finished()
{
  if (!connected()) { return; }
  uint32_t current_limit = tic_settings_get_current_limit(settings.get_pointer());
  current_limit = tic_settings_achievable_current_limit(settings.get_pointer(), current_limit);
  tic_settings_set_current_limit(settings.get_pointer(), current_limit);
  handle_settings_changed();
}

void main_controller::handle_decay_mode_input(uint8_t decay_mode)
{
  if (!connected()) { return; }
  tic_settings_set_decay_mode(settings.get_pointer(), decay_mode);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_soft_error_response_input(uint8_t soft_error_response)
{
  if (!connected()) { return; }
  tic_settings_set_soft_error_response(settings.get_pointer(), soft_error_response);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_soft_error_position_input(int32_t soft_error_position)
{
  if (!connected()) { return; }
  tic_settings_set_soft_error_position(settings.get_pointer(), soft_error_position);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_current_limit_during_error_input(int32_t current_limit_during_error)
{
  if (!connected()) { return; }
  tic_settings_set_current_limit_during_error(settings.get_pointer(), current_limit_during_error);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_current_limit_during_error_input_finished()
{
  if (!connected()) { return; }
  uint32_t current_limit_during_error =
    tic_settings_get_current_limit_during_error(settings.get_pointer());
  current_limit_during_error = tic_settings_achievable_current_limit(
    settings.get_pointer(), current_limit_during_error);
  tic_settings_set_current_limit_during_error(settings.get_pointer(), current_limit_during_error);
  handle_settings_changed();
}

void main_controller::handle_disable_safe_start_input(bool disable_safe_start)
{
  if (!connected()) { return; }
  tic_settings_set_disable_safe_start(settings.get_pointer(), disable_safe_start);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_ignore_err_line_high_input(bool ignore_err_line_high)
{
  if (!connected()) { return; }
  tic_settings_set_ignore_err_line_high(settings.get_pointer(), ignore_err_line_high);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_auto_clear_driver_error_input(bool auto_clear_driver_error)
{
  if (!connected()) { return; }
  tic_settings_set_auto_clear_driver_error(settings.get_pointer(), auto_clear_driver_error);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_never_sleep_input(bool never_sleep)
{
  if (!connected()) { return; }
  tic_settings_set_never_sleep(settings.get_pointer(), never_sleep);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_vin_calibration_input(int16_t vin_calibration)
{
  if (!connected()) { return; }
  tic_settings_set_vin_calibration(settings.get_pointer(), vin_calibration);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_upload_complete()
{
  // After a firmware upgrade is complete, allow the GUI to reconnect to the
  // device automatically.
  disconnected_by_user = false;
}

void main_controller::handle_pin_func_input(uint8_t pin, uint8_t func)
{
  if (!connected()) { return; }
  tic_settings_set_pin_func(settings.get_pointer(), pin, func);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_pin_pullup_input(uint8_t pin, bool pullup)
{
  if (!connected()) { return; }
  tic_settings_set_pin_pullup(settings.get_pointer(), pin, pullup);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_pin_polarity_input(uint8_t pin, bool polarity)
{
  if (!connected()) { return; }
  tic_settings_set_pin_polarity(settings.get_pointer(), pin, polarity);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::handle_pin_analog_input(uint8_t pin, bool analog)
{
  if (!connected()) { return; }
  tic_settings_set_pin_analog(settings.get_pointer(), pin, analog);
  settings_modified = true;
  handle_settings_changed();
}

void main_controller::set_target_position(int32_t position)
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    device_handle.set_target_position(position);
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }
}

void main_controller::set_target_velocity(int32_t velocity)
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    device_handle.set_target_velocity(velocity);
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }
}

void main_controller::halt_and_set_position(int32_t position)
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    device_handle.halt_and_set_position(position);
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }
}

void main_controller::halt_and_hold()
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    device_handle.halt_and_hold();
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }
}

void main_controller::deenergize()
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    device_handle.deenergize();
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }
}

// TODO: rename to resume to match the button name
void main_controller::energize()
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    device_handle.energize();

    device_handle.exit_safe_start();
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }
}

void main_controller::start_input_setup()
{
  if (!connected()) { return; }

  if (settings_modified)
  {
    window->show_info_message("This wizard cannot be used right now because "
      "the settings you changed have not been applied to the device.\n"
      "\n"
      "Please click \"Apply settings\" to apply your changes to the device or "
      "select \"Reload settings from device\" in the Device menu to discard "
      "your changes, then try again.");
    return;
  }

  uint8_t control_mode = tic_settings_get_control_mode(cached_settings.get_pointer());
  switch (control_mode)
  {
  case TIC_CONTROL_MODE_RC_POSITION:
  case TIC_CONTROL_MODE_RC_SPEED:
  case TIC_CONTROL_MODE_ANALOG_POSITION:
  case TIC_CONTROL_MODE_ANALOG_SPEED:
    // The Tic is using a valid control mode; do nothing and continue.
    break;

  default:
    window->show_info_message("This wizard helps you set the scaling "
      "parameters for the Tic's RC or analog input.\n"
      "\n"
      "Please change the control mode to RC or analog, then try again.");
    return;
  }

  deenergize();
  window->run_input_wizard(control_mode);
}

void main_controller::apply_settings()
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    tic::settings fixed_settings = settings;
    std::string warnings;
    fixed_settings.fix(&warnings);
    if (warnings.empty() ||
      window->confirm(warnings.append("\nAccept these changes and apply settings?")))
    {
      settings = fixed_settings;
      device_handle.set_settings(settings);
      device_handle.reinitialize();
      handle_settings_applied();
      settings_modified = false;  // this must be last in case exceptions are thrown
    }
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }

  handle_settings_changed();
}

void main_controller::open_settings_from_file(std::string filename)
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    std::string settings_string = read_string_from_file(filename);
    tic::settings fixed_settings = tic::settings::read_from_string(settings_string);
    std::string warnings;
    fixed_settings.fix(&warnings);
    if (warnings.empty() ||
      window->confirm(warnings.append("\nAccept these changes and load settings?")))
    {
      settings = fixed_settings;
      settings_modified = true;
    }
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }

  handle_settings_changed();
}

void main_controller::save_settings_to_file(std::string filename)
{
  if (!connected()) { return; }

  try
  {
    assert(connected());

    tic::settings fixed_settings = settings;
    std::string warnings;
    fixed_settings.fix(&warnings);
    if (!warnings.empty())
    {
      if (window->confirm(warnings.append("\nAccept these changes and save settings?")))
      {
        settings = fixed_settings;
        settings_modified = true;
      }
      else
      {
        return;
      }
    }
    std::string settings_string = settings.to_string();
    write_string_to_file(filename, settings_string);
  }
  catch (std::exception const & e)
  {
    show_exception(e);
  }

  handle_settings_changed();
}

void main_controller::reload_variables()
{
  assert(connected());

  try
  {
    variables = device_handle.get_variables(true);
    variables_update_failed = false;
  }
  catch (...)
  {
    variables_update_failed = true;
    throw;
  }
}

bool main_controller::control_mode_is_serial(tic::settings const & s) const
{
  uint8_t control_mode = tic_settings_get_control_mode(s.get_pointer());
  return (control_mode == TIC_CONTROL_MODE_SERIAL);
}
