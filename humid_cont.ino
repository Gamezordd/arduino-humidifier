
/*************************************************************
  This example runs directly on ESP8266 chip.
  
  Change WiFi ssid, pass, and Blynk auth token to run
  Feel free to apply it to any other example. It's simple!
 *************************************************************/

/* Comment this out to disable prints and save space */
// #define BLYNK_PRINT Serial

// Change to false to disable debug prints
static bool PRINT_LOGS = false;

#include "credentials.h"
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include "DHTStable.h"
#include <time.h>
#include <TimeLib.h>
#include <string.h>
#include <EEPROM.h>

DHTStable DHT;
// Humidifier control
#define REED D5
#define TRANSDUCER D3
#define R1 D2
#define R2 D1
#define R3 D4
#define DHT11_PIN D6

// Max number of async (periodic) jobs allowed
#define JOB_LIMIT 100
#define HUMIDITY_RANGE 5
#define SCHEDULE_SNOOZE_EEPROM_ADDRESS 5

#define AUTO_OFF_TIME 10 * 60 * 60 * 1000 // 10 hours
#define TIME_RETRY_DURATION 5 * 60 * 1000 // 5 mins before next try to fetch time

#define STATE_VPIN V0
#define HUMIDITY_VPIN V1
#define TARGET_HUMIDITY_VPIN V2
#define TEMPERATURE_VPIN V3
#define LOW_WATER_VPIN V4
#define SCHEDULE_VPIN V5
#define USE_SCHEDULE_VPIN V7

// App Toggle State
static int deviceState = 0;

// Sensor Readings
static int currentHumidity = 0;
static int targetHumidity = 0;
static int currentTemprature = 0;
static int reedState = 0;

static int transducerState = 0;

static int timeQueryFailureTimestamp = 0;

// Schedule
static int startTimeInMins = 0;
static int endTimeInMins = 0;
static bool scheduleActive = false;
static bool useSchedule = false;

// Used to store the timestamp till which the schedule should not run, to EEPROM. (For power failure cases)
static int scheduleSnoozedUntil = 0;

static int autoOfftimer = 0;

static int lastRuns[JOB_LIMIT] = {};

void handleSchedule()
{
  if (!useSchedule)
    return;

  if (startTimeInMins == endTimeInMins)
  {
    if (PRINT_LOGS)
      Serial.println("Schedule not set");
    return;
  }

  int currentTime = getCurrentTime();
  if (currentTime == -1)
    return;
  if (currentTime <= scheduleSnoozedUntil)
  {
    return;
  }
  int currentDeviceState = deviceState;
  if (PRINT_LOGS)
  {
    Serial.print("Current time: ");
  }
  Serial.println(currentTime);

  if (!scheduleActive && currentTime >= startTimeInMins && currentTime <= endTimeInMins)
  {
    scheduleActive = true;
    currentDeviceState = 1;
  }
  else if (scheduleActive && currentTime >= endTimeInMins)
  {
    scheduleActive = false;
    currentDeviceState = 0;
  }

  if (currentDeviceState != deviceState)
  {
    deviceState = currentDeviceState;
    Blynk.virtualWrite(STATE_VPIN, deviceState);
  }
}

int getCurrentTime()
{
  struct tm timeinfo;
  int retryCount = 0;
  // Wait for time to be set
  while (millis() - timeQueryFailureTimestamp > TIME_RETRY_DURATION && !getLocalTime(&timeinfo) && retryCount < 3)
  {
    if (PRINT_LOGS)
      Serial.print("Waiting for time: ");
    retryCount++;
    // Wait for 1 second before retrying
    delay(1000);
  }

  if (retryCount == 3 || (!getLocalTime(&timeinfo) && (millis() - timeQueryFailureTimestamp <= TIME_RETRY_DURATION)))
  {
    if (PRINT_LOGS)
    Serial.println("Failed to get time");
    timeQueryFailureTimestamp = millis();
    return -1;
  }

  // Extract and print hours and minutes
  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;

  if (PRINT_LOGS){
    Serial.printf("Hour: %02d, Minute: %02d\n", currentHour, currentMinute);
  }
  return currentHour * 60 + currentMinute;
}

// Save any time the schedule is overriden by the user to EEPROM to handle power failures
void handleScheduleSnooze(int updatedTransducerState)
{
  int currentTime = getCurrentTime();
  if (PRINT_LOGS){
    Serial.print("Time in mins: ");
    Serial.println(currentTime);
  }

  if (currentTime == -1)
    return;

  if (useSchedule && !updatedTransducerState && currentTime >= startTimeInMins && currentTime <= endTimeInMins)
  {
    scheduleSnoozedUntil = endTimeInMins;
  }
  else if (useSchedule && updatedTransducerState && currentTime >= endTimeInMins)
  {
    scheduleSnoozedUntil = startTimeInMins;
  }

  if (useSchedule && scheduleSnoozedUntil != 0)
  {
    int existingSnooze = EEPROM.read(SCHEDULE_SNOOZE_EEPROM_ADDRESS);
    if (existingSnooze != scheduleSnoozedUntil)
    {
      EEPROM.write(SCHEDULE_SNOOZE_EEPROM_ADDRESS, scheduleSnoozedUntil);
      EEPROM.commit();
      if (PRINT_LOGS)
      {
        Serial.print("Schedule snoozed until: ");
        Serial.println(scheduleSnoozedUntil);
      }
    }
  }
}

void getScheduleSnooze()
{
  scheduleSnoozedUntil = EEPROM.read(SCHEDULE_SNOOZE_EEPROM_ADDRESS);
}

void getTimeSetup()
{
  double timezone = 5.5;
  int dst = 0;
  configTime(timezone * 3600, dst, "129.6.15.28", "216.239.35.0", "128.138.141.172");
  while (!time(nullptr))
  {
    Serial.print(".");
    delay(100);
  }
}

void syncTime()
{
  // synchronize internal clock to NTP
  time_t timeNow;
  time(&timeNow);
  setTime(timeNow);
}

void throttledRun(void (*func)(), unsigned long interval, int jobId)
{
  if (jobId > JOB_LIMIT)
    return;
  unsigned long now = millis();
  if (now - lastRuns[jobId] >= interval)
  {
    lastRuns[jobId] = now;
    func();
  }
}

void readVitals()
{
  int chk = DHT.read11(DHT11_PIN);
  int humidity = DHT.getHumidity();
  int temperature = DHT.getTemperature();

  if (humidity != currentHumidity)
  {
    Blynk.virtualWrite(HUMIDITY_VPIN, humidity);
  }
  if (temperature != currentTemprature)
  {
    Blynk.virtualWrite(TEMPERATURE_VPIN, temperature);
  }

  currentHumidity = humidity;
  currentTemprature = temperature;

  if (PRINT_LOGS)
  {
    Serial.print("Humidity: ");
    Serial.println(humidity);
    Serial.print("Temprature: ");
    Serial.println(temperature);
    Serial.print("State: ");
    Serial.println(transducerState);
    Serial.print("Power: ");
    Serial.println(deviceState);
    Serial.println("");
  }
}

void readReed()
{
  // Accmodate for reed being INPUT_PULLUP (switch connects to ground)
  int reed = !digitalRead(REED);
  if (reed != reedState)
  {
    Blynk.virtualWrite(LOW_WATER_VPIN, !reed);
  }
  reedState = reed;
  if (PRINT_LOGS)
  {
    Serial.print("Reed: ");
    Serial.println(reedState);
    Serial.println("");
  }
}

void controlTransducer()
{
  int updatedTransducerState = LOW;
  bool shouldStart = deviceState == 1 && reedState == 1;

  if (currentHumidity < targetHumidity - HUMIDITY_RANGE)
  {
    updatedTransducerState = HIGH;
  }
  else if (transducerState == 1 && currentHumidity < targetHumidity)
  {
    updatedTransducerState = HIGH;
  }

  updatedTransducerState = updatedTransducerState & shouldStart;

  if (transducerState != updatedTransducerState)
  {
    if (PRINT_LOGS)
    {
      Serial.print("set transducer: ");
      Serial.println(updatedTransducerState);
    }
    digitalWrite(TRANSDUCER, updatedTransducerState);
    transducerState = updatedTransducerState;
  }
}

BLYNK_CONNECTED()
{
  Blynk.syncVirtual(HUMIDITY_VPIN);
  Blynk.syncVirtual(TARGET_HUMIDITY_VPIN);
  Blynk.syncVirtual(STATE_VPIN);
  Blynk.syncVirtual(SCHEDULE_VPIN);
  Blynk.syncVirtual(USE_SCHEDULE_VPIN);
}

BLYNK_WRITE(TARGET_HUMIDITY_VPIN)
{
  targetHumidity = param.asInt();
  controlTransducer();
}

BLYNK_WRITE(STATE_VPIN)
{
  deviceState = param.asInt();
  handleScheduleSnooze(deviceState);
  if (PRINT_LOGS)
  {
    Serial.println("Power: ");
    Serial.println(deviceState);
  }

  if (deviceState == 1)
  {
    autoOfftimer = millis() + AUTO_OFF_TIME;
  }
  controlTransducer();
}

BLYNK_WRITE(SCHEDULE_VPIN)
{
  startTimeInMins = param[0].asInt() / 60;
  endTimeInMins = param[1].asInt() / 60;
  if (PRINT_LOGS)
  {
    Serial.print("Start time: ");
    Serial.println(startTimeInMins);
    Serial.print("End time: ");
    Serial.println(endTimeInMins);
  }
}

BLYNK_WRITE(USE_SCHEDULE_VPIN)
{
  useSchedule = param.asInt();
  if (PRINT_LOGS)
  {
    Serial.print("Use schedule: ");
    Serial.println(useSchedule);
  }
}

void loop()
{
  if (millis() > autoOfftimer)
  {
    if (deviceState == 1)
    {
      Blynk.virtualWrite(STATE_VPIN, deviceState);
      deviceState = 0;
    }
  }
  throttledRun(readVitals, 60000, 0);
  throttledRun(readReed, 1000, 1);
  throttledRun(syncTime, 24 * 60 * 60000, 2);
  throttledRun(handleSchedule, 10 * 60000, 3);
  controlTransducer();
  Blynk.run();
  delay(500);
}

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(512);
  getScheduleSnooze();
  pinMode(TRANSDUCER, OUTPUT);
  pinMode(REED, INPUT_PULLUP);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  readVitals();
  getTimeSetup();

  autoOfftimer = AUTO_OFF_TIME;
}