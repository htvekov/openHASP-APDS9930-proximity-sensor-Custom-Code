/*
MIT License - Copyright (c) 2019-2022 Francis Van Roie
For full license information read the LICENSE file in the project folder
Template - Original openHASP Custom Code example

Some parts of this custom code originates from the APDS9930 library examples
Open Source - (c) Davide Depau, December 11, 2015
Please use, reuse, and modify these files as you see fit. Please maintain attribution to SparkFun Electronics and release anything derivative under the same license.

Open Source - (c) 2023 Henning Tvekov
APDS9930 proximity- and ambient light sensor Custom Code for openHASP
Please use, reuse, and modify these files as you see fit. Please maintain all attributions

********************************************************
** Custom openHASP code for APDS9930 proximity sensor **
********************************************************

Custom code for handling idle_off command internally in openHASP code upon proximity detection
Setting of custom brightness level (low or high) depending on APDS9930 sensors ambient light lux value
This will make it possible to turn on the display upon proximity detection at night at low brightness level
   
Proximity detection, value and ambient light lux level are written to log and published via MQTT custom topic
Example:
topic: hasp/sunton_02/state/custom
payload: {"proximity":620,"lux":5}
Typical time consumption for entire detection, ambient light lux reading, commands issuing, MQTT messages and log writes are some 115 milliseconds.

Proximity detection throttle flag is set upon detection and is reset in the void custom_every_5seconds() loop
This effectively reduces code time consumption for proximity detection to max. once every five seconds

Ambient light lux value is read every 60 seconds and written to log
Sensor value is also added to- and exposed with openHASP 'standard' sensor MQTT message (TelePeriod settings interval)
Example:
topic: hasp/sunton_02/state/sensors
payload: {"time":"2023-04-28T21:23:10","uptimeSec":2700,"uptime":"0T00:45:00","lux":5}

brightness_low, brightness_high and ambient_light_threshold variables can all be set via custom MQTT command and hence controlled dynamically at runtime
Example - HA Developer tools:
service: mqtt.publish
data:
  topic: hasp/sunton_02/command/custom/brightness_low
  payload: 25


When proximity is registered above threshold value, an idle_off command is instantly fired internally in the openHASP code (wake up device)
This is followed by a backlight={'state':1,'brightness':xx} command, where brightness value is set to either brightness_low or brightness_high depending on what
ambient light lux level has been read upon proximity detection

When using openHASP Home Assistant Custom Component, the idle_off command will also trigger a state and brightness commmand from the CC
As the brightness level from CC is fixed, this will unfortunately instantly owerwrite any brightness setting done in custom code
So until this gets fixed in the CC, a hack is needed in openHASP Custom Components light.py file in order to ignore actions on idle_off commands for specific openHASP devices

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
            # skip device = device MQTT nodename + " " + "backlight"
            #
            skip_device ="sunton_02 backlight"     
            if self.name == skip_device and message == HASP_IDLE_OFF:
                return
            else:
                new_state = {"state": backlight, "brightness": brightness}
*/


#include "hasplib.h"

#if defined(HASP_USE_CUSTOM) && true // <-- set this to true in your code

#include "hasp_debug.h"
#include "custom/my_custom.h"

#define DUMP_REGS
#include "Wire.h"
#include "APDS9930.h" // Include external APDS9930 library. Download from: https://github.com/depau/APDS9930

// Pins
#define APDS9930_SDA    19 // Share I2C with touch 
#define APDS9930_SCL    20 // Share I2C with touch

// Constants
#define PROX_THRESHOLD          400 // Proximity threshold level (values: 0 - 1023)
#define POLL_INTERVAL           125 // Custom code poll interval in millis (8 times pr. second)

// Global variables
APDS9930 apds = APDS9930();
uint16_t proximity_data = 0;
volatile bool throttle_flag = false;
unsigned long mytime= 0;
uint8_t mycount_5sec= 0;
float ambient_lux = 0;
uint16_t ch0 = 0;
uint16_t ch1 = 1;
uint8_t brightness_payload = 0;
// Following three variables can be altered via custom MQTT message
uint8_t brightness_low = 25; // 10% full brightness - night time
uint8_t brightness_high = 191; // 75% full brightness (Sunton 7" device max. brightness without flicker)
uint16_t ambient_light_threshold = 5; // Ambient light threshold for use of either low- or high brightness

void custom_setup()
{ 
    // init mytime
    mytime = millis();
    
    // Initialize I2C
    Wire.begin(APDS9930_SDA, APDS9930_SCL); // (SDA, SCL)
    LOG_INFO(TAG_CUSTOM, "*** APDS-9930 - Proximity Sensor ***");
    
    // Initialize APDS-9930 (configure I2C and initial values)
    if ( apds.init() ) {
        LOG_INFO(TAG_CUSTOM, "APDS-9930 initialization complete");
    } else {
        LOG_INFO(TAG_CUSTOM, "Something went wrong during APDS-9930 init!");
    }
  
    // Adjust the Proximity sensor gain
    if ( !apds.setProximityGain(PGAIN_2X) ) {
        LOG_INFO(TAG_CUSTOM, "Something went wrong trying to set PGAIN");
    }
  
    // Start running the APDS-9930 proximity sensor (NO interrupts)
    if ( apds.enableProximitySensor(false) ) {
        LOG_INFO(TAG_CUSTOM, "Proximity sensor is now running");
    } else {
        LOG_INFO(TAG_CUSTOM, "Something went wrong during sensor init!");
    }

    // Start running the APDS-9930 light sensor (NO interrupts)
    if ( apds.enableLightSensor(false) ) {
        LOG_INFO(TAG_CUSTOM, "Light sensor is now running");
    } else {
        LOG_INFO(TAG_CUSTOM, "Something went wrong during light sensor init!");
    }

    #ifdef DUMP_REGS
    /* Register dump */
    uint8_t reg;
    uint8_t val;

    for(reg = 0x00; reg <= 0x19; reg++) {
      if( (reg != 0x10) && \
          (reg != 0x11) ) {
              apds.wireReadDataByte(reg, val);
              Serial.print(reg, HEX);
              Serial.print(": 0x");
              Serial.println(val, HEX);
      }
    } 
    apds.wireReadDataByte(0x1E, val);
    Serial.print(0x1E, HEX);
    Serial.print(": 0x");
    Serial.println(val, HEX);
    #endif
}

void custom_loop()
{
    if ( (millis() - mytime > POLL_INTERVAL) && not throttle_flag ) {
        // Reset mytime to current loop start time
        mytime = millis();
 
        if ( !apds.readProximity(proximity_data) ) {
            LOG_INFO(TAG_CUSTOM, "Error reading proximity value!");
        }
        
        if ( proximity_data > PROX_THRESHOLD ) {
            dispatch_text_line("idle off", TAG_CUSTOM);
            LOG_INFO(TAG_CUSTOM, "Proximity detected. Level: %d", proximity_data);
            // Set throttle flag only if detection match defined proximity limits
            throttle_flag = true;
            
            if (  !apds.readAmbientLightLux(ambient_lux) ) {
                      LOG_INFO(TAG_CUSTOM, "Error reading ambient light lux value!");
            } else {
                LOG_INFO(TAG_CUSTOM, "Ambient light lux: %d", uint16_t (ambient_lux));
            }
            
            // Set low/high payload brightness level depending on ambient light lux level
            if ( uint16_t (ambient_lux) < ambient_light_threshold ) {
                brightness_payload = brightness_low;
            } else {
                brightness_payload = brightness_high;
            }
            char payload[64];
                snprintf_P(payload, sizeof(payload), PSTR("backlight={'state':1,'brightness':%d}"), brightness_payload);
                dispatch_text_line(payload, TAG_CUSTOM);
            
            // Push custom MQTT message with proximity- and ambient light lux level
            snprintf_P(payload, sizeof(payload), PSTR("{\"proximity\":%d,\"lux\":%d}"), proximity_data, uint16_t (ambient_lux) );
            dispatch_state_subtopic("custom",payload);
        
            // Test log - Time spend in custom_loop
            // LOG_INFO(TAG_CUSTOM, "Millis spent: %d", millis() - mytime );
        }
    }
}

void custom_interruptRoutine()
{
    // Not used
}

void custom_every_second()
{
    // Not used
}

void custom_every_5seconds()
{
    // Update ambient_lux variable every minute
    mycount_5sec++;
    if ( mycount_5sec == 12 ) {
        mycount_5sec = 0;
        if (  !apds.readAmbientLightLux(ambient_lux) ||
                      !apds.readCh0Light(ch0) || 
                      !apds.readCh1Light(ch1) ) {
                            LOG_INFO(TAG_CUSTOM, "Error reading ambient light lux value!");
                    } else {
                            LOG_INFO(TAG_CUSTOM, "Ambient light lux: %d", uint16_t (ambient_lux));
        }
    }
    if ( throttle_flag ) {
        throttle_flag = false;
        LOG_INFO(TAG_CUSTOM, "Clear proximity throttle flag");
    }
}

bool custom_pin_in_use(uint8_t pin)
{
    switch(pin) {
        // Define custom code pins
        case APDS9930_SDA:
        case APDS9930_SCL:
            return true;
        default:
            return false;
    }
}

void custom_get_sensors(JsonDocument& doc)
{
    doc[F("lux")] = uint16_t(ambient_lux);
}

void custom_topic_payload(const char* topic, const char* payload, uint8_t source)
{
    LOG_INFO(TAG_CUSTOM, "Custom MQTT message: %s => %s", topic, payload);
    
    if ( !strcmp(topic, "brightness_low") ) {
        brightness_low = atoi(payload);
        LOG_INFO(TAG_CUSTOM, "New brightness low value: %d", brightness_low);
    
    } else if ( !strcmp(topic, "brightness_high") ) {
        brightness_high = atoi(payload);
        LOG_INFO(TAG_CUSTOM, "New brightness high value: %d", brightness_high);
    
    } else if ( !strcmp(topic, "ambient_light_threshold") ) {
        ambient_light_threshold = atoi(payload);
        LOG_INFO(TAG_CUSTOM, "New ambient light threshold value: %d", ambient_light_threshold);
    
    } else {
        LOG_INFO(TAG_CUSTOM, "Error - Unknown custom MQTT message!");
    }
}

#endif
