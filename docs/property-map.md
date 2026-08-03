# Thermostat property map

This inventory is extracted from string-named property registration/update calls in the supplied Ghidra C export. The MQTT bridge publishes every non-sensitive scalar observed in native bucket JSON under `<base_topic>/property/...`; the Home Assistant climate mapping is a smaller, semantically verified subset.

## Home Assistant climate properties

| Home Assistant property | Native source | Read | Write | Confidence / notes |
|---|---|---|---|---|
| Current temperature | Device bucket `+0x100`; `current_temperature` JSON | Yes | No | Direct Q16.16 fallback plus passive JSON |
| Current humidity | `current_humidity`/`humidity` JSON | When observed | No | No verified direct getter |
| Target temperature | Shared bucket `+0x160`; `target_temperature` | Yes | Yes | Native cloud apply helper |
| Target low | Client vtable `+0xc8`; `target_temperature_low` | Yes | Yes | Setter vtable `+0xc4` through native apply |
| Target high | Client vtable `+0xc0`; inferred high property | Yes | Yes | Setter vtable `+0xbc` through native apply |
| HVAC mode | Client vtable `+0x10c`; schedule/type JSON | Yes | Yes | Setter vtable `+0x108`; enum mapping inferred |
| HVAC action | `hvac_heater_state`, `hvac_ac_state`, `hvac_fan_state` | When observed | No | Converted to heating/cooling/fan/idle |
| Eco preset | Client vtable `+0x5c`; `eco`, `manual_eco_all` | Yes | Experimental | Setter vtable `+0x58`; ABI inferred from native callers |
| Emergency heat | Client vtable `+0xb8`; `emer_heat_enable` | Yes | Yes | Setter vtable `+0xb4` through native apply |
| Fan state/speed | `hvac_fan_state`, `fan_current_speed`, `fan_cooling_state` | When observed | No | No setter mapped confidently |
| Battery | Device bucket `+0x110`; `battery_level` | Yes | No | Units need physical-device validation |

## Command support summary

- **Verified native routing:** target temperature, range low, range high, HVAC/switch-over mode, emergency heat.
- **Experimental native routing:** eco preset.
- **Read-only:** current temperature, humidity, battery, HVAC action, fan state/speed, capability and diagnostic fields.
- **Generic MQTT access:** all scalar fields below when they appear in serialized native bucket JSON.

## Properties registered through `FUN_00052a18`

These 130 names are primarily shared/configuration, setpoint, installation, learning, safety, and settings properties.

- `active_rcs_sensors`
- `alt_heat_delivery`
- `alt_heat_source`
- `alt_heat_x2_delivery`
- `alt_heat_x2_source`
- `auto_away`
- `auto_away_enable`
- `auto_away_learning`
- `auto_away_reset`
- `auto_dehum_enabled`
- `aux_heat_delivery`
- `aux_heat_source`
- `away_temperature_high`
- `away_temperature_high_adjusted`
- `away_temperature_high_enabled`
- `away_temperature_low`
- `away_temperature_low_adjusted`
- `away_temperature_low_enabled`
- `backplate_mono_version`
- `boiler_setpoint`
- `click_sound`
- `compressor_lockout_timeout`
- `cooling_delivery`
- `cooling_source`
- `cooling_x2_delivery`
- `cooling_x2_source`
- `cooling_x3_delivery`
- `cooling_x3_source`
- `country_code`
- `current_schedule_mode`
- `dehumidifier_fan_activation`
- `dehumidifier_orientation_selected`
- `dehumidifier_type`
- `demand_charge_icon`
- `device_locale`
- `diamond_changed_location`
- `dr_reminder_enabled`
- `dual_fuel_breakpoint`
- `dual_fuel_breakpoint_override`
- `dual_fuel_selected`
- `eco_onboarding_needed`
- `emer_heat_delivery`
- `emer_heat_enable`
- `emer_heat_source`
- `error_code`
- `fan_cooling_enabled`
- `fan_cooling_readiness`
- `farsight_screen`
- `filter_changed_date`
- `filter_changed_set_date`
- `filter_reminder_enabled`
- `filter_reminder_level`
- `filter_replacement_needed`
- `filter_replacement_threshold_sec`
- `has_air_filter`
- `has_fossil_fuel`
- `has_hot_water_control`
- `has_hot_water_temperature`
- `heat_link_connection`
- `heat_link_heat_type`
- `heat_link_hot_water_type`
- `heat_link_manual_mode`
- `heat_link_model`
- `heat_link_sw_version`
- `heat_pump_comp_threshold`
- `heat_pump_comp_threshold_enabled`
- `heat_x2_delivery`
- `heat_x2_source`
- `heat_x3_delivery`
- `heat_x3_source`
- `heater_delivery`
- `heater_source`
- `hot_water_away_enabled`
- `house_type`
- `humidifier_fan_activation`
- `humidifier_type`
- `humidity_control_lockout_enabled`
- `humidity_control_lockout_end_time`
- `humidity_control_lockout_start_time`
- `hvac_safety_shutoff_enabled`
- `hvac_smoke_safety_shutoff_enabled`
- `is_furnace_shutdown`
- `leaf`
- `logging_priority`
- `manual_eco_all`
- `manual_eco_timestamp`
- `manual_schedule`
- `multiroom_active`
- `name`
- `nest_thermostat_last_active`
- `nlclient_state`
- `num_thermostats`
- `oob_interview_completed`
- `oob_startup_completed`
- `oob_summary_completed`
- `oob_temp_completed`
- `oob_test_completed`
- `oob_where_completed`
- `oob_wifi_completed`
- `oob_wires_completed`
- `postal_code`
- `preconditioning_enabled`
- `pro_id`
- `rcs_control_setting`
- `renovation_date`
- `safety_state`
- `safety_state_time`
- `safety_temp_activating_hvac`
- `schedule_learning_reset`
- `sensor_schedule`
- `should_wake_on_approach`
- `smoke_shutoff_supported`
- `star_type`
- `sunlight_correction_active`
- `target_temperature`
- `target_temperature_low`
- `target_temperature_type`
- `temperature_lock`
- `temperature_lock_high_temp`
- `temperature_lock_low_temp`
- `temperature_lock_pin_hash`
- `temperature_scale`
- `thermostat_alert`
- `time_to_target`
- `tou_icon`
- `touched_by.touched_by`
- `where_id`
- `wheres`
- `wiring_error`
- `wiring_error_timestamp`

## Properties updated through `FUN_000744a8`

These 72 names are primarily runtime telemetry, capabilities, HVAC state, wiring, sensor events, and device status properties.

- `auto_dehum_state`
- `battery_level`
- `can_cool`
- `can_heat`
- `country_code`
- `current_temperature`
- `custom_schedule`
- `cycle_features`
- `dehumidifier_state`
- `demand_charge_events`
- `dr_event`
- `eco`
- `fan_capabilities`
- `fan_cooling_state`
- `fan_current_speed`
- `fleet_rhr_device`
- `has_air_filter`
- `has_alt_heat`
- `has_aux_heat`
- `has_dehumidifier`
- `has_dual_fuel`
- `has_emer_heat`
- `has_fan`
- `has_fossil_fuel`
- `has_heat_pump`
- `has_hot_water_control`
- `has_hot_water_temperature`
- `has_humidifier`
- `has_x2_alt_heat`
- `has_x2_cool`
- `has_x2_heat`
- `has_x3_cool`
- `has_x3_heat`
- `hot_water_active`
- `hot_water_boiling_state`
- `house_type`
- `humidifier_state`
- `hvac_ac_state`
- `hvac_alt_heat_state`
- `hvac_alt_heat_x2_state`
- `hvac_aux_heater_state`
- `hvac_cool_x2_state`
- `hvac_cool_x3_state`
- `hvac_emer_heat_state`
- `hvac_fan_state`
- `hvac_heat_x2_state`
- `hvac_heat_x3_state`
- `hvac_heater_state`
- `hvac_pins`
- `hvac_staging_ignore`
- `hvac_wires`
- `last_updated_at`
- `local_ip`
- `num_thermostats`
- `pin_c_description`
- `pin_g_description`
- `pin_ob_description`
- `pin_rc_description`
- `pin_rh_description`
- `pin_star_description`
- `pin_w1_description`
- `pin_w2aux_description`
- `pin_y1_description`
- `pin_y2_description`
- `postal_code`
- `renovation_date`
- `safety_temp_activating_hvac`
- `sensor_event`
- `sensor_schedule`
- `smoke_shutoff_supported`
- `touched_by.touched_by`
- `tuneups`

## Sensitive data policy

The property mirror rejects any flattened key whose path contains credential-like terms such as `password`, `passwd`, `secret`, `credential`, `assigned_cred`, `token`, or `private_key`. The complete raw JSON object is never published as a single payload.

## Interpretation cautions

A string name proves that the native application registers or updates a property; it does not by itself prove a public setter, units, enum semantics, or availability on every hardware configuration. Fields outside the Home Assistant table are therefore exposed generically and remain read-only unless a native setter was traced.
