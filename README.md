# arduino-humidifier
A simple humidifier sketch to connect to blynk app on a smartphone and control a humidifier with a simple external relay circuit. Supports remote monitoring of room temps, humidity using a DHT11 sensor and low water detection using a reed switch. Supports automatic cutoff to maintain a certain humidity level as well as scheduled start/stop.

# credentials.h
To use the sketch with custom credentials, create a `credentials.h` file in the root folder with the following config:

```
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define BLYNK_TEMPLATE_ID "Template_Id"
#define BLYNK_TEMPLATE_NAME "Template_Name"
#define BLYNK_AUTH_TOKEN "Auth_Token"

const char ssid[] = "wifi_ssid";
const char pass[] = "wifi_password";

#endif
```