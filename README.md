# esp8266-homebridge-reader
A sketch to read a thermometer sensor value from Homebridge instance and display it on a SSD1306-based OLED display. 

## What it does
This sketch automatically toggles two screens in a 5 sec interval (``displayInterval``):
1. A large, easily readable thermometer value (in °C) along with the sensor name refreshed every 15 seconds.
2. The last 24 hours chart along with min/max values. It tries to be a bit clever, so it keeps the values in 120 buckets (12 minutes average each) instead of keeping minute data for the whole day. Also, the algorithm starts to render the chart immediately (as a line when plugged initially in) displaying the values that have been collected once the device has booted, until the whole daily buffer is filled. The last value collected (the live temperature) is blinking in the rightmost part of the chart.

## How to use
- Set the ``ssid`` and ``password`` values to connect to your WiFi
- Set the ``hb_host``, ``hb_username`` and ``hb_password`` for your Homebridge instance. Note the Bojnour domain name resolution might be sketchy on ESP, so if that does not work reliabily for you, try the HB IP address instead.
- Use curl (see below) to determine the ``uniqueId``s for the sensors you'd like to read. In this sketch, I read one Jablotron temperature sensor value.

### DNS resolution notice
If you use Bonjour/mDNS for your Homebridge server (e.g. ``homebridge.local``), it may not work reliabily -- it is a known issue on an ESP8266. This means the device might not be able to resolve a Bonjour DNS address.
To mitigate this, either switch to a classic DNS (e.g. ``homebridge`` or ``homebridge.lan``), if you're running a DNS server in your network or use a plain IP address to connect.

## How to get the device IDs
Pleae note I'm using the default Docker-based Homebridge config - default port, HTTP.

### Log in
```sh
curl -X POST "http://homebridge-address-here:8581/api/auth/login" -H "Content-Type: application/json" -d '{"username":"admin-username-here","password":"admin-password-here"}'
```
### Use the bearer JWT to list all devices
```sh
curl -X GET "http://homebridge-address-here:8581/api/accessories" -H "Authorization: Bearer your-JWT-here"
```

The output might be rather large, so either use your favorite text editor search or a LLM to determine what accessory ID (``uniqueId``) to use.

## Use case example
I've puth the ESP8266 (on a cheap Wemos D1 clone board) along with the display in a simple plastic box and connected it to a temperature sensor (via Homebridge).

<img width="2048" height="1536" alt="IMG_4531" src="https://github.com/user-attachments/assets/f186c7a2-9bfb-4066-84cd-7ac7259bd634" />
<img width="2048" height="1536" alt="IMG_4532" src="https://github.com/user-attachments/assets/ae2972b8-51a8-4c10-b98e-e2c4f7be9964" />

