# esp8266-homebridge-reader
A sketch to read a thermometer and humidity sensor value from Homebridge instance and display it on a SSD1306-based OLED display. This sketch displays a fixed value on the first line and toggles temperature and humidity on the second line.

## How to use
- Set the ssid and password values to connect to your WiFi
- Set the hb_host, hb_username and hb_password for your Homebridge instance. Note the Bojnour domain name resolution might be sketchy on ESP, so if that does not work reliabily, use the HB IP address instead.
- Use curl (see below) to determine the uniqueIds for the sensors you'd like to read. In this sketch, I read one Jablotron temperature sensor and a Tado thermostatic valve combined temperature + humidity sensor value.

## How to get the device IDs
Pleae note I'm using the default Docker-based Homebridge config (default port, HTTP).

### Log in
curl -X POST "http://homebridge-address-here:8581/api/auth/login"   -H "Content-Type: application/json"   -d '{"username":"admin-username-here","password":"admin-password-here"}'

### Use the bearer JWT to list all devices
curl -X GET "http://homebridge-address-here:8581/api/accessories"   -H "Authorization: Bearer your-JWT-here"

The output might be rather large, so either use your favorite text editor search or a LLM to determine what accessory ID (uniqueId) to use.
