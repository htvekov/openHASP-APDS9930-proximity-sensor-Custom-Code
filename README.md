# openHASP APDS-9930 proximity sensor Custom Code documentation v1.01

![APDS-9930 DIL package](https://github.com/htvekov/openHASP-APDS9930-proximity-sensor-Custom-Code/blob/main/APDS9930_DIL.PNG) ![APDS-9930 board module](https://github.com/htvekov/openHASP-APDS9930-proximity-sensor-Custom-Code/blob/main/apds9930.PNG) ![Sunton connnectors](https://github.com/htvekov/openHASP-APDS9930-proximity-sensor-Custom-Code/blob/main/sunton.PNG)

### Revision:
- **1.01** (2023-05-01)
Improved documentation
- **1.00** (2023-04-29)
Initial Custom Code release

# APDS-9930 proximity sensor properties:

The APDS-9930 Digital Proximity and Ambient Light Sensor is a cheap, realiable and very small sensor perfectly suited for near display proximity detection on any openHASP device. The actual sensors measurements (appx. 4 x 2 mm) makes it very suitable to install behind the glass display edge on e.g. the [T3E](https://github.com/HASwitchPlate/openHASP/discussions/458) device or in the enclosure frame for e.g. Sunton devices. Opposite to typical PIR sensors, this sensor will only detect very close proximity within 100 mm. This makes the sensor a perfect candidate to instantly wake up an openHASP device before user actually touch the display

[Data Sheet](https://datasheetspdf.com/datasheet/APDS-9930.html) description:
- The APDS-9930 provides digital Ambient Light Sensing (ALS), IR LED and a complete proximity detection system in a single 8 pin package
- The proximity function offers plug and play detection to 100 mm (without front glass) thus eliminating the need for factory calibration of the end equipment or sub-assembly
- The proximity detection feature operates well from bright sunlight to dark rooms
- The wide dynamic range also allows for operation in short distance detection behind dark glass such as a cell phone.

# openHASP Custom Code for [APDS-9930 proximity sensor](https://www.aliexpress.com/item/32846656029.html)

- Custom code for issuing `idle_off` command locally in openHASP code upon proximity detection
- Supports custom brightness level (low or high) depending on APDS-9930 sensors ambient light lux value. This feature will make it possible to wake up the openHASP device, upon proximity detection at night, at low brightness level
- Custom brightness- and ambient light threshold levels are configurable at runtime via MQTT commands
- All proximity detection events, distance value and ambient light lux level are written to log and published via MQTT custom topic. Typical time consumption for entire detection, ambient light lux reading, issuing commands / MQTT messages and log writes are some 115 milliseconds

### Custom MQTT message example:

```yaml
topic: hasp/sunton_02/state/custom
payload: {"proximity":620,"lux":5}
```

### Log example - Proximity detection on openHASP plate from `idle_long` state:
```yaml
[19:53:46.848][61428/73772 16][22028/24228 10] MSGR: idle=off
[19:53:46.863][61428/70340 12][22028/24228 10] MQTT PUB: idle => off
[19:53:46.878][59380/68408 13][22028/24228 10] CUST: Proximity detected. Level: 817
[19:53:46.894][57332/66468 13][22028/24228 10] CUST: Ambient light lux: 20
[19:53:46.909][55284/66132 16][22028/24228 10] MSGR: backlight={'state':1,'brightness':191}
[19:53:46.924][61428/72176 14][22028/24228 10] HASP: First touch Disabled
[19:53:46.942][59380/68648 13][22028/24228 10] MQTT PUB: backlight => {"state":"on","brightness":191}
[19:53:46.959][59380/70592 15][22028/24228 10] MQTT PUB: custom => {"proximity":817,"lux":20}
[19:53:48.683][61428/73772 16][22028/24228 10] CUST: Clear proximity throttle flag
```

- Proximity detection throttle flag is set upon detection and is reset in the `void custom_every_5seconds()` loop. This effectively reduces time consumption in code for continuous proximity detection to max. once every five seconds
- Ambient light lux value is read every 60 seconds and written to log. Sensor value is also added to- and exposed with openHASPs generic `sensor` MQTT message (`TelePeriod` settings define the interval)

### openHASP sensors MQTT publish example:

```yaml
topic: hasp/sunton_02/state/sensors
payload: {"time":"2023-04-28T21:23:10","uptimeSec":2700,"uptime":"0T00:45:00","lux":5}
```

#### *`brightness_low`, `brightness_high` and `ambient_light_threshold` variables can all be set via custom MQTT command and hence controlled dynamically at runtime 🚀*

### Usage scheme upon proximity detection:

| Ambient light level <= `ambient_light_threshold`   | Ambient light level > `ambient_light_threshold`     |
| -------------------------------------------------- | --------------------------------------------------- |
| `brightness_low` value used for brightness setting | `brightness_high` value used for brightness setting |
 
### Example - Setting `brightness_low` via HA Developer tools:
```yaml
service: mqtt.publish
data:
  topic: hasp/sunton_02/command/custom/brightness_low
  payload: 25
```

When proximity is registered above defined threshold value, an `idle_off` command is instantly fired internally in the openHASP code (wake up device). This is followed by a `backlight={'state':1,'brightness':xx}` command, where brightness value is set to either `brightness_low` or `brightness_high`,  depending on what ambient light lux level has been read upon proximity detection

> ***Note***
> When using openHASP Home Assistant Custom Component, the `idle_off` command will also trigger a state and brightness command from the CC. As the brightness level from CC is fixed, this will unfortunately instantly owerwrite any brightness setting done in custom code. So until this gets fixed in the CC, a hack is needed in openHASP Custom Components `lights.py` file in order to ignore actions on `idle_off` commands for specific openHASP devices

### Hack example - openHASP Custom Component `lights.py` file:

```python
   async def async_listen_idleness(self):
        """Listen to messages on MQTT for HASP idleness."""

        @callback
        async def idle_message_received(msg):
            """Process MQTT message from plate."""
            message = HASP_IDLE_SCHEMA(msg.payload)

            if message == HASP_IDLE_OFF:
                brightness = self._awake_brightness
                backlight = 1
            elif message == HASP_IDLE_SHORT:
                brightness = self._idle_brightness
                backlight = 1
            elif message == HASP_IDLE_LONG:
                brightness = self._awake_brightness
                backlight = 0
            else:
                return
            #
            # Skip IDLE_OFF commands for specific openHASP devices
            # skip device = device MQTT nodename
            #
            skip_device ="sunton_02"     
            if self.name == skip_device + " backlight" and message == HASP_IDLE_OFF:
                return
            else:
                new_state = {"state": backlight, "brightness": brightness}

            _LOGGER.debug(
                "Idle state for %s is %s - Dimming to %s; Backlight to %s",
                self.name,
                message,
                brightness,
                backlight,
            )
```

### Pin connection example between APDS-9930 module board and a 7" Sunton 8048S070C device

| openHASP device | APDS-9930 Board | Function  |
| --------------- | --------------- | --------- |
| 3.3V            | VCC             | Power     |
| 3.3V            | VL              | Power     |
| GND             | GND             | Ground    |
| GPIO19          | SDA             | I²C Data  |
| GPIO20          | SCL             | I²C Clock |
| NC              | INT             | Interrupt |

> ***Note***
> The APDS-9930 library shares the global `Wire` object with the openHASP device touch controller using default I²C bus. This restricts the APDS-9930 sensors SDA/SCL GPIO pins to be *identical* with the openHASP device defined touch controller GPIO pins ! On the 5- and 7" Sunton devices all needed pins are easily accessible via the P3 and P4 JST connectors. Interrupt pin (INT) is *not* connected nor used in this Custom Code. IMPORTANT: APDS-9930 sensor is *not* 5V tolerant - Connect to 3.3V only !!


### Config keywords:

- Add git+https://github.com/depau/APDS9930.git to lib-deps in `platformio.ini` file
- Copy `my_custom.h` and `my_custom.ccp` files to openhasp/src/custom folder
- Revise `platformio_override.ini` and `user_config_override.h` files
- Patch openHASP Custom Config `lights.py` file in Home Assistant or copy file from this repo instead

Suggestions, improvements, error reporting etc. are very welcome ! 🙂

April, 2023 @htvekov
