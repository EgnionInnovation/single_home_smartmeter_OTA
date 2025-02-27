// Comment the line below for single standalone SM
//  #define MULTI_UNIT

// Libraries
#include <Arduino.h>
#include "esp_mac.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <Wire.h>
#include <driver/adc.h>
#include <ATM90E3x.h>
#include "IPEM_Hardware.h"
// #include <WiFiManager.h>
#include "OTA_cert.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include "time.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <cstdlib> //AZURE
#include <string.h>
#include <time.h>
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"
#include <mqtt_client.h>

Preferences preferences;

const char *mqtt_server = "192.168.18.60";
// **************** FUNCTIONS AND ROUTINES ****************
unsigned long previousMillis_mqttUpdate = 0; // will store last time MQTT values were updated
const long period_mqttUpdate = 5000;         // period at which MQTT values are updated
int daylight_saving_state = 2;
String MAC_ADDRESS;

#define debug true

// WiFi Manager -------------------------------------------------------------------------
#define CAPTIVE_PORTAL_TIMEOUT 60
String mqtt_host_ip = "", device_key = "", meter_name = "", hardware_version = "";
String in_topic, out_topic;

// OTA parameters ---------------------------------------------------------------------------------
String FirmwareVer = {"3.03"};
#define URL_fw_Version "https://raw.githubusercontent.com/EgnionInnovation/single_home_smartmeter_OTA/main/firmware_version.txt"
#define URL_fw_Bin "https://raw.githubusercontent.com/EgnionInnovation/single_home_smartmeter_OTA/main/fw/firmware.bin"
void firmwareUpdate();
int FirmwareVersionCheck(); 

unsigned long previousMillis = 0; // will store last time LED was updated
unsigned long previousMillis_2 = 0;
const long interval = 60000;
const long mini_interval = 1000;

void repeatedCall()
{
  static int num = 0;
  unsigned long currentMillis = millis();
  if ((currentMillis - previousMillis) >= interval)
  {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    if (FirmwareVersionCheck())
    {
      firmwareUpdate();
    }
  }
  if ((currentMillis - previousMillis_2) >= mini_interval)
  {
    previousMillis_2 = currentMillis;
    if (WiFi.status() == WL_CONNECTED)
    {
      // if(debug)
      // Serial.println("wifi connected");
    }
    else
    {
      if (WiFi.status() != WL_CONNECTED)
      {
        // couldn't connect
        if (debug)
          Serial.println("[main] Couldn't connect to WiFi after multiple attempts");
        delay(5000);
        ESP.restart();
      }
      if (debug)
        Serial.println("Connected");
    }
  }
}

void firmwareUpdate(void)
{
  WiFiClientSecure OTA_client;
  OTA_client.setCACert(OTAcert);
  httpUpdate.setLedPin(2, LOW);
  t_httpUpdate_return ret = httpUpdate.update(OTA_client, URL_fw_Bin);

  switch (ret)
  {
  case HTTP_UPDATE_FAILED:
    if (debug)
      Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    if (debug)
      Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    if (debug)
      Serial.println("HTTP_UPDATE_OK");
    break;
  }
}

int FirmwareVersionCheck(void)
{
  Serial.println(FirmwareVer);
  String payload;
  int httpCode;
  String fwurl = "";
  fwurl += URL_fw_Version;
  fwurl += "?";
  fwurl += String(rand());
  if (debug)
    Serial.println(fwurl);
  WiFiClientSecure *OTA_client = new WiFiClientSecure;

  if (OTA_client)
  {
    OTA_client->setCACert(OTAcert);

    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
    HTTPClient https;

    if (https.begin(*OTA_client, fwurl))
    { // HTTPS
      if (debug)
        Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      delay(100);
      httpCode = https.GET();
      delay(100);
      if (httpCode == HTTP_CODE_OK) // if version received
      {
        payload = https.getString(); // save received version
      }
      else
      {
        if (debug)
        {
          Serial.print("error in downloading version file:");
          Serial.println(httpCode);
        }
      }
      https.end();
    }
    delete OTA_client;
  }

  if (httpCode == HTTP_CODE_OK) // if version received
  {
    payload.trim();
    if (payload.equals(FirmwareVer))
    {
      if (debug)
        Serial.printf("\nDevice already on latest firmware version:%s\n", FirmwareVer);
      return 0;
    }
    else
    {
      if (debug)
      {
        Serial.println(payload);
        Serial.println("New firmware detected");
      }
      return 1;
    }
  }
  return 0;
}
//---------------------------------------------------------------  *OTA setup* ---------------------------------------------------------------

//---------------------------------------------------------------  *Time setup* ---------------------------------------------------------------
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;
int daylightOffset_sec = 0;

struct tm timeinfo;

String ntp_time = "";

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    if (debug)
      Serial.println("Failed to obtain time");
    return;
  }

  char timeSec[3];
  strftime(timeSec, 3, "%S", &timeinfo);
  char timeMin[3];
  strftime(timeMin, 3, "%M", &timeinfo);
  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  char timeDay[10];
  strftime(timeDay, 10, "%d", &timeinfo);
  char timeWeek[3];
  strftime(timeWeek, 3, "%w", &timeinfo);
  char timeMonth[3];
  strftime(timeMonth, 3, "%m", &timeinfo);
  char timeYear[5];
  strftime(timeYear, 5, "%Y", &timeinfo);
  char timeZone[4];
  strftime(timeZone, 4, "%z", &timeinfo);

  if (String(timeMonth).toInt() == 11 & timeWeek == 0 & String(timeDay).toInt() < 8 & daylight_saving_state != 0)
  {
    daylightOffset_sec = 0;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    daylight_saving_state = 0;
  }

  if (String(timeMonth).toInt() == 3 & timeWeek == 0 & String(timeDay).toInt() > 24 & daylight_saving_state != 1)
  {
    daylightOffset_sec = 3600;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    daylight_saving_state = 1;
  }

  if (!getLocalTime(&timeinfo))
  {
    if (debug)
      Serial.println("Failed to obtain time");
    return;
  }

  ntp_time = String(timeYear) + "-";
  ntp_time += String(timeMonth) + "-";
  ntp_time += String(timeDay) + " ";
  ntp_time += String(timeHour) + ":";
  ntp_time += String(timeMin) + ":";
  ntp_time += String(timeSec) + " ";
  ntp_time += String(timeZone) + ":00";
}
//---------------------------------------------------------------  *Time setup* ---------------------------------------------------------------

//---------------------------------------------------------------  *MQTT setup* ---------------------------------------------------------------

String meter_ID = "Meter000";
String meter_status = "";
String message_title;

int number_of_CTs = 0;
int Ratio_CT1, Ratio_CT2, Ratio_CT3;

int ch_wattage;
int current_wattage = 0;
int ch_time;

WiFiClient espClient;
long lastMsg = 0;
char msg[50];
int value = 0;
//---------------------------------------------------------------  *MQTT setup* ---------------------------------------------------------------

// Return Register Values from the ATM90E32, or ATM90E36, - and Display
void DisplayRegisters(boolean DisplayFull = true)
{

  // Display Board Configuration
  if (EnableDisplayBoardConfiguration == true && DisplayFull == true)
    DisplayBoardConfiguration();

  // **************** VOLTAGE ****************

  // Get Mains Voltages
  LineVoltage1 = NoiseFilterSquelch(eic.GetLineVoltage1(), 1);
  LineVoltage2 = NoiseFilterSquelch(eic.GetLineVoltage2(), 1);
  LineVoltage3 = NoiseFilterSquelch(eic.GetLineVoltage3(), 1);
  LineVoltageAverage = 0;
  LineVoltageTotal = 0;

// Calculate Voltage
#if ATM_SINGLEVOLTAGE == true && ATM_SPLITPHASE == true
  Serial.println("* SOFTWARE CONFIGURATION ERROR *\n* You cannot have ATM_SINGLEVOLTAGE and ATM_SPLITPHASE, both Enabled.\n");
#else
#if ATM_SPLITPHASE == true
  LineVoltageTotal = (LineVoltage1 + LineVoltage3) / 2; // Split Single Phase configured, so only 120V per leg
#else
  LineVoltageAverage = (LineVoltage1 + LineVoltage2 + LineVoltage3) / 3; // Voltage should be around 110V, or 220-240, at the AC transformer primary.
#endif
#endif

  if (DisplayFull == true) // Display Expanded Information
  {

    // Mains RMS Voltage
    PrintUnderline("Mains RMS Voltage");

#if ATM_SINGLEVOLTAGE == false

    Serial.println("Voltage 1: " + String(LineVoltage1) + " V");

#if ATM_SPLITPHASE == false
    Serial.println("Voltage 2: " + String(LineVoltage2) + " V");
#endif

    Serial.println("Voltage 3: " + String(LineVoltage3) + " V");

#if ATM_SPLITPHASE == true
    PrintSeparator("Total Split Voltage: " + String(LineVoltageTotal) + " V (V1~X~V3)");
// #else
//     PrintSeparator("Average Voltage: " + String(LineVoltageAverage) + " V (V1~V2~V3)");
#endif

#else
    Serial.println("Voltage 1: " + String(LineVoltage1) + " V (Single Line Voltage Selected)");
#endif
  }

  // **************** CURRENT ****************

  // Get Total Current.  Ignore CT2 if Split Phase
  LineCurrentCT1 = NoiseFilterSquelch(eic.GetLineCurrentCT1());
#if ATM_SPLITPHASE == false
  LineCurrentCT2 = NoiseFilterSquelch(eic.GetLineCurrentCT2());
#else
  LineCurrentCT2 = 0;
#endif
  LineCurrentCT3 = NoiseFilterSquelch(eic.GetLineCurrentCT3());

  // Using ESP32 ADC for CT4 Current Input

#if ATM90DEVICE == ATM90E32_DEVICE || CT4_CONFIG == CT4_ESP
  ReadCT4Current();
#if CT4_ENABLED == true
  LineCurrentTotal = NoiseFilterSquelch(LineCurrentCT1 + LineCurrentCT2 + LineCurrentCT3 + LineCurrentCT4);
#else
  LineCurrentTotal = NoiseFilterSquelch(LineCurrentCT1 + LineCurrentCT2 + LineCurrentCT3);
#endif
#endif

// E36 Using I4N Input for Current
#if ATM90DEVICE == ATM90E36_DEVICE && CT4_CONFIG == CT4_ATM
  LineCurrentCTN = NoiseFilterSquelch(eic.GetLineCurrentCTN());
  LineCurrentTotal = NoiseFilterSquelch(LineCurrentCT1 + LineCurrentCT2 + LineCurrentCT3);
#endif

  if (DisplayFull == true) // Display Expanded Information
  {
    Serial.println("");

    // Mains RMS Current
    PrintUnderline("Mains RMS Current");
    Serial.println("Current CT1: " + String(LineCurrentCT1) + " A");

#if ATM_SPLITPHASE == false
    Serial.println("Current CT2: " + String(LineCurrentCT2) + " A");
#endif

    Serial.println("Current CT3: " + String(LineCurrentCT3) + " A");

#if ATM90DEVICE == ATM90E32_DEVICE && CT4_ENABLED == true || CT4_CONFIG == CT4_ESP && CT4_ENABLED == true
    Serial.println("Current CT4: " + String(LineCurrentCT4) + " A (ESP ADC1 CH7)");
#endif

#if ATM90DEVICE == ATM90E36_DEVICE && CT4_CONFIG == CT4_ATM
    Serial.println("Current CTN: " + String(LineCurrentCTN) + " A");
#endif

#if ATM_SPLITPHASE == true
    Serial.println("Actual Total Current: " + String(LineCurrentTotal) + " A (CT1~X~CT3)");
#else
#if CT4_ENABLED == true
    PrintSeparator("Actual Total Current: " + String(LineCurrentTotal) + " A (CT1~CT2~CT3~CT4)");
#else
    Serial.println("Actual Total Current: " + String(LineCurrentTotal) + " A (CT1~CT2~CT3)");
#endif
#endif

    Serial.println("");
  }

  // **************** POWER ****************

  // Calculated Power CT2ased on V*I
#if ATM_SINGLEVOLTAGE == true // Single Mains Voltage for all Current Measurements
  CalculatedPowerCT1 = LineVoltage1 * LineCurrentCT1;
  CalculatedPowerCT2 = LineVoltage1 * LineCurrentCT2;
  CalculatedPowerCT3 = LineVoltage1 * LineCurrentCT3;

#if ATM90DEVICE == ATM90E36_DEVICE
  CalculatedPowerCT4 = LineVoltage1 * LineCurrentCT4;
#endif

  if (DisplayFull == true) // Display Expanded Information
  {
    PrintUnderline("Calculated RMS Power");
    Serial.println("Power V1*I1: " + String(CalculatedPowerCT1) + " W");
    Serial.println("Power V1*I2: " + String(CalculatedPowerCT2) + " W");
    Serial.println("Power V1*I3: " + String(CalculatedPowerCT3) + " W");

#if ATM90DEVICE == ATM90E36_DEVICE
    Serial.println("Power V1*I4: " + String(CalculatedPowerCT4) + " W");
#endif
  }
#else // Seperate Mains Voltages for Current Measurements
  CalculatedPowerCT1 = LineVoltage1 * LineCurrentCT1;
  CalculatedPowerCT2 = LineVoltage2 * LineCurrentCT2;
  CalculatedPowerCT3 = LineVoltage3 * LineCurrentCT3;

#if ATM90DEVICE == ATM90E36_DEVICE
  CalculatedPowerCT4 = LineVoltage1 * LineCurrentCT4; // ESP32 ADC1 CH7 using V1
#endif

  if (DisplayFull == true) // Display Expanded Information
  {
    PrintUnderline("Calculated RMS Power");
    Serial.println("Power V1*I1: " + String(CalculatedPowerCT1) + " W");
    Serial.println("Power V2*I2: " + String(CalculatedPowerCT2) + " W");
    Serial.println("Power V3*I3: " + String(CalculatedPowerCT3) + " W");
  }
#if CT4_ENABLED == true && ATM90DEVICE == ATM90E36_DEVICE
  if (DisplayFull == true) // Display Expanded Information
  {
    Serial.println("Power V1*I4: " + String(CalculatedPowerCT4) + " W");
  }
#endif
#endif

// Calculate Total Watts
#if ATM_SPLITPHASE == true // USA 120+120
  CalculatedTotalPower = (LineVoltage1 * LineCurrentCT1) + (LineVoltage3 * LineCurrentCT3);

  if (DisplayFull == true) // Display Expanded Information
    PrintSeparator("Calculated Total Power: " + String(CalculatedTotalPower) + " W (CT1~X~CT3)");
#else // World
#if CT4_CONFIG == CT4_ESP && CT4_ENABLED == true
  CalculatedTotalPower = (LineVoltage1 * LineCurrentCT1) + (LineVoltage2 * LineCurrentCT2) + (LineVoltage3 * LineCurrentCT3) + (LineVoltage1 * LineCurrentCT4);

  if (DisplayFull == true) // Display Expanded Information
    PrintSeparator("Calculated Total Power: " + String(CalculatedTotalPower) + " W (CT1~CT2~CT3~CT4)");
#else
  CalculatedTotalPower = (LineVoltage1 * LineCurrentCT1) + (LineVoltage2 * LineCurrentCT2) + (LineVoltage3 * LineCurrentCT3);

  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Calculated Total Power: " + String(CalculatedTotalPower) + " W (CT1~CT2~CT3)");
#endif
#endif

  if (DisplayFull == true) // Display Expanded Information
    Serial.println("");

  // Active Power.  Extra Import/Export values included to provide added detail for Home Automation logging
  String ActiveFlag = "";

  if (DisplayFull == true) // Display Expanded Information
    PrintUnderline("Active Power (Absorbed or Used by the Load)");

  // Active Power CT1 - Always a Postive Number
  ActivePowerCT1 = NoiseFilterSquelch(eic.GetActivePowerCT1(), 0.2);
  ActivePowerImportCT1 = 0;
  ActivePowerExportCT1 = 0;
  ActiveFlag = "";

  if (ActivePowerCT1 > 0)
  {
    ActivePowerImportCT1 = ActivePowerCT1;
    ActiveFlag = "(Import)";
  }

  if (ActivePowerCT1 < 0)
  {
    ActivePowerExportCT1 = -ActivePowerCT1;
    ActiveFlag = "(Export)";
  }

  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Active Power CT1: " + String(ActivePowerCT1) + " W \t" + ActiveFlag);

  // Active Power CT2 - Always a Postive Number
  ActivePowerCT2 = NoiseFilterSquelch(eic.GetActivePowerCT2(), 0.2);
  ActivePowerImportCT2 = 0;
  ActivePowerExportCT2 = 0;
  ActiveFlag = "";

  if (ActivePowerCT2 > 0)
  {
    ActivePowerImportCT2 = ActivePowerCT2;
    ActiveFlag = "(Import)";
  }

  if (ActivePowerCT2 < 0)
  {
    ActivePowerExportCT2 = -ActivePowerCT2;
    ActiveFlag = "(Export)";
  }
  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Active Power CT2: " + String(ActivePowerCT2) + " W \t" + ActiveFlag);

  // Active Power CT3 - Always a Postive Number
  ActivePowerCT3 = NoiseFilterSquelch(eic.GetActivePowerCT3(), 0.2);
  ActivePowerImportCT3 = 0;
  ActivePowerExportCT3 = 0;
  ActiveFlag = "";

  if (ActivePowerCT3 > 0)
  {
    ActivePowerImportCT3 = ActivePowerCT3;
    ActiveFlag = "(Import)";
  }

  if (ActivePowerCT3 < 0)
  {
    ActivePowerExportCT3 = -ActivePowerCT3;
    ActiveFlag = "(Export)";
  }

  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Active Power CT3: " + String(ActivePowerCT3) + " W \t" + ActiveFlag);

  // Total Active Power
  TotalActivePower = NoiseFilterSquelch(eic.GetTotalActivePower(), 0.2);
  ActiveFlag = "";

  if (TotalActivePower > 0)
  {
    TotalActivePowerImport = TotalActivePower;
    ActiveFlag = "(Import)";
  }

  if (TotalActivePower < 0)
  {
    TotalActivePowerExport = -TotalActivePower;
    ActiveFlag = "(Export)";
  }

  CalculatedTotalActivePower = (ActivePowerCT1 + ActivePowerCT2 + ActivePowerCT3);

  if (DisplayFull == true) // Display Expanded Information
  {
    Serial.println("Total Active Power: " + String(TotalActivePower) + " W \t" + ActiveFlag);
    Serial.println("Calculated Total Active Power: " + String(CalculatedTotalActivePower) + " W (CT1~CT2~CT3)");
    Serial.println("");
  }

  // Re-Active Power
  ReactivePowerCT1 = NoiseFilterSquelch(eic.GetReactivePowerCT1());
  ReactivePowerCT2 = NoiseFilterSquelch(eic.GetReactivePowerCT2());
  ReactivePowerCT3 = NoiseFilterSquelch(eic.GetReactivePowerCT3());
  TotalReactivePower = NoiseFilterSquelch(eic.GetTotalReactivePower());
  CalculatedTotalReactivePower = (ReactivePowerCT1 + ReactivePowerCT2 + ReactivePowerCT3);

  if (DisplayFull == true) // Display Expanded Information
  {
    Serial.println("Re-Active Power");
    Serial.println("ReActive Power CT1: " + String(ReactivePowerCT1) + " VAR");
    Serial.println("ReActive Power CT2: " + String(ReactivePowerCT2) + " VAR");
    Serial.println("ReActive Power CT3: " + String(ReactivePowerCT3) + " VAR");
    Serial.println("Total ReActive Power: " + String(TotalReactivePower) + " VAR (CT1~CT2~CT3)");

    Serial.println("Calculated Total Reactive Power: " + String(CalculatedTotalReactivePower) + " W (CT1~CT2~CT3)");
    Serial.println("");
  }

  // Apparent Power Flowing - Voltage and Current (V * I)
  ApparentPowerCT1 = NoiseFilterSquelch(eic.GetApparentPowerCT1());
  ApparentPowerCT2 = NoiseFilterSquelch(eic.GetApparentPowerCT2());
  ApparentPowerCT3 = NoiseFilterSquelch(eic.GetApparentPowerCT3());
  TotalApparentPower = NoiseFilterSquelch(eic.GetTotalApparentPower());
  CalculatedTotalApparentPower = (ApparentPowerCT1 + ApparentPowerCT2 + ApparentPowerCT3);

  if (DisplayFull == true) // Display Expanded Information
  {
    Serial.println("Apparent Power (Total Amount of Power Flowing from Source to Load)");
    Serial.println("Apparent Power CT1: " + String(ApparentPowerCT1) + " VA");
    Serial.println("Apparent Power CT2: " + String(ApparentPowerCT2) + " VA");
    Serial.println("Apparent Power CT3: " + String(ApparentPowerCT3) + " VA");
    Serial.println("Total Apparent Power: " + String(TotalApparentPower) + " VA (CT1~CT2~CT3)");

    Serial.println("Calculated Total Apparent Power: " + String(CalculatedTotalApparentPower) + " W (CT1~CT2~CT3)");
    Serial.println("\n");
  }

#if ATM90DEVICE == ATM90E32_DEVICE
  if (DisplayFull == true) // Display Expanded Information
    PrintUnderline("Other Power Information");

  // Total Fundamental Power
  TotalActiveFundPower = NoiseFilterSquelch(eic.GetTotalActiveFundPower());
  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Total Fundamental Power: " + String(TotalActiveFundPower) + " pH(t)\n");

  // Total Harmonic Power
  TotalActiveHarPower = NoiseFilterSquelch(eic.GetTotalActiveHarPower());
  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Total Harmonic Power: " + String(TotalActiveHarPower) + " ");

#endif

  if (DisplayFull == true)
    Serial.println("");

  // **************** OTHER ****************

  // Power Factor
  PowerFactorCT1 = NoiseFilterSquelch(eic.GetPowerFactorCT1(), 0.01, false);
  PowerFactorCT2 = NoiseFilterSquelch(eic.GetPowerFactorCT2(), 0.01, false);
  PowerFactorCT3 = NoiseFilterSquelch(eic.GetPowerFactorCT3(), 0.01, false);
  TotalPowerFactor = NoiseFilterSquelch(eic.GetTotalPowerFactor(), 0, false);

  if (DisplayFull == true) // Display Expanded Information
  {
    PrintUnderline("Power Factor");
    Serial.println("Power Factor CT1: " + String(PowerFactorCT1) + " PF");
    Serial.println("Power Factor CT2: " + String(PowerFactorCT2) + " PF");
    Serial.println("Power Factor CT3: " + String(PowerFactorCT3) + " PF");
    Serial.println("Total Power Factor: " + String(TotalPowerFactor) + " PF");
    Serial.println("");
  }

  // Phase Angle
  PhaseAngleCT1 = NoiseFilterSquelch(eic.GetPhaseCT1(), 0, true, 180);
  PhaseAngleCT2 = NoiseFilterSquelch(eic.GetPhaseCT2(), 0, true, 180);
  PhaseAngleCT3 = NoiseFilterSquelch(eic.GetPhaseCT3(), 0, true, 180);

  if (DisplayFull == true) // Display Expanded Information
  {
    PrintUnderline("Phase Angle");
    Serial.println("Phase Angle CT1: " + String(PhaseAngleCT1));
    Serial.println("Phase Angle CT2: " + String(PhaseAngleCT2));
    Serial.println("Phase Angle CT3: " + String(PhaseAngleCT3));
    Serial.println("");
  }

  if (DisplayFull == true) // Display Expanded Information
    PrintUnderline("Other Information");

  // Chip Temperature
  ChipTemperature = NoiseFilterSquelch(eic.GetTemperature());

  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Chip Temperature: " + String(ChipTemperature) + " °C\n");

  // Line Frequency
  LineFrequency = NoiseFilterSquelch(eic.GetFrequency());

  if (DisplayFull == true) // Display Expanded Information
    Serial.println("Mains Frequency: " + String(LineFrequency) + " Hz\n");

  // Read PCB NTC Temperature
  if (DisplayFull == true)
    ReadPCBTemperature(); // Read PCB NTC Temperature

  if (DisplayFull == true)
    Serial.println("");

  // Read PCB DCV_IN Voltage
  if (DisplayFull == true)
    ReadADCVoltage(); // Read AC>DC Input Voltage
}

// Local credentials server ------------------------------------------------------------
String SSID, Pass;
const char *ssid = "Abacus_SM_Captive_Portal";
const char *password = "12345678";

DNSServer dnsServer;
AsyncWebServer localserver(80);
IPAddress apIP(192, 168, 4, 1);
volatile bool dataSaved = false;
unsigned long startMillis;
bool timerPaused = false;

// Function to start the server
void startServer()
{
  // Start Access Point
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  if (debug)
  {
    Serial.println("Access Point started");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());
  }

  // DNS spoofing: Redirect all DNS queries to the ESP32's IP
  dnsServer.start(53, "*", apIP);

  // Handle root requests ("/")
  localserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->redirect("/landing"); });

  // Handle landing page requests
  localserver.on("/landing", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
    String landingPage = "<html><head><title>QR Code Captive Portal</title></head><body>";
    landingPage += "<h1>Welcome to the QR Captive Portal</h1>";
    landingPage += "<p>Scan the QR code to send your data:</p>";
    landingPage += "</body></html>";
    request->send(200, "text/html", landingPage); });

  // Handle GET requests with query parameters on "/scan"
  localserver.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
    if (request->hasParam("ID"))
      meter_ID = request->getParam("ID")->value();
    if (request->hasParam("Host"))
      mqtt_host_ip = request->getParam("Host")->value();
    if (request->hasParam("DeviceKey"))
      device_key = request->getParam("DeviceKey")->value();
    if (request->hasParam("HWver"))
      hardware_version = request->getParam("HWver")->value();
    if (request->hasParam("SSID"))
      SSID = request->getParam("SSID")->value();
    if (request->hasParam("Pass"))
      Pass = request->getParam("Pass")->value();

    if (meter_ID.length() > 15)
    {
      String meter_ID1 = meter_ID.substring(0, 15);
      String meter_ID2 = meter_ID.substring(15);
      preferences.putString("Meter_ID1", meter_ID1);
      preferences.putString("Meter_ID2", meter_ID2);
    }
    else
    {
      // Otherwise, store the entire meter_ID in Meter_ID1 and leave Meter_ID2 empty.
      preferences.putString("Meter_ID1", meter_ID);
      preferences.putString("Meter_ID2", "");
    }

    int len = device_key.length();
	const int segmentLength = 15;  // Maximum characters per segment
	
	// Split device_key into up to 4 segments:
	String device_key1 = device_key.substring(0, min(segmentLength, len));
	String device_key2 = (len > segmentLength) ? device_key.substring(segmentLength, min(segmentLength * 2, len)) : "";
	String device_key3 = (len > segmentLength * 2) ? device_key.substring(segmentLength * 2, min(segmentLength * 3, len)) : "";
	String device_key4 = (len > segmentLength * 3) ? device_key.substring(segmentLength * 3, len) : "";
	
	// Store each segment in preferences:
	preferences.putString("Device_Key1", device_key1);
	preferences.putString("Device_Key2", device_key2);
	preferences.putString("Device_Key3", device_key3);
	preferences.putString("Device_Key4", device_key4);

    preferences.putString("mqtt_host_ip", mqtt_host_ip);
    preferences.putString("hw_version", hardware_version);
    preferences.putString("wifi_ssid", SSID);
    preferences.putString("wifi_pass", Pass);

	    // Log saved parameters
    Serial.println("Parameters saved to EEPROM:");
    Serial.println("ID: " + meter_ID);
    Serial.println("Host: " + mqtt_host_ip);
    Serial.println("Device: " + device_key);
    Serial.println("HWver: " + hardware_version);
    Serial.println("SSID: " + SSID);
    Serial.println("Pass: " + Pass);

    // Mark data as saved
    dataSaved = true;

    String response = "<html><head>";
    response += "<meta http-equiv='refresh' content='5;url=/thankyou' />";
    response += "</head><body><h1>Data Saved Successfully</h1>";
    response += "<p>Thank you! The server will shut down shortly.</p>";
    response += "</body></html>";

    request->send(200, "text/html", response); });

  // Handle thank-you page
  localserver.on("/thankyou", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send(200, "text/html", "<html><body><h1>Goodbye!</h1></body></html>"); });

  // Start the web server
  localserver.begin();
  if (debug)
  {
    Serial.println("Web server running...");
  }
}

// Function to stop the server
void stopServer()
{
  dnsServer.stop();
  localserver.end();
  WiFi.softAPdisconnect(true);
  if (debug)
  {
    Serial.println("Server and Access Point stopped");
  }
}

// WiFi Event Handlers
void onClientConnected(arduino_event_id_t event, arduino_event_info_t info)
{
  timerPaused = true;
  if (debug)
  {
    Serial.println("Client connected. Timer paused.");
  }
}

void onClientDisconnected(arduino_event_id_t event, arduino_event_info_t info)
{
  timerPaused = false;
  startMillis = millis(); // Reset the timer when client disconnects
  if (debug)
  {
    Serial.println("Client disconnected. Timer resumed.");
  }
}

void manageServer(int durationSeconds)
{
  startMillis = millis();
  while (true)
  {
    dnsServer.processNextRequest();

    // Stop server if data is saved
    if (dataSaved)
    {
      delay(5000); // Show confirmation page for 5 seconds
      stopServer();
      return;
    }

    // Check timer unless paused
    if (!timerPaused && (millis() - startMillis >= durationSeconds * 1000))
    {
      if (debug)
      {
        Serial.println("Time limit reached. Shutting down server.");
      }
      stopServer();
      return;
    }
  }
}

void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, Pass);
  if (debug)
  {
    Serial.print("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial.print('.');
      delay(1000);
    }
    Serial.println(WiFi.localIP());
  }
}

// Azure IOT variables and functions
// When developing for your own Arduino-based platform, please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 1440

// Translate iot_configs.h defines into variables used by the sample
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static const char *mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static char *device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;
String AZURE_SUB_TOPIC = "devices/" + String(device_id) + "/messages/devicebound";

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client AZUREclient;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint32_t telemetry_send_count = 0;
static String telemetry_payload = "{}";

#define INCOMING_DATA_BUFFER_SIZE 512
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

StaticJsonDocument<512> AzurejsonDoc; // Adjust size as needed
// Define separate buffers for topic and data
char topic_buffer[INCOMING_DATA_BUFFER_SIZE];
char data_buffer[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &AZUREclient,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  (void)handler_args;
  (void)base;
  (void)event_id;

  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
#else  // ESP_ARDUINO_VERSION_MAJOR
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
#endif // ESP_ARDUINO_VERSION_MAJOR
  switch (event->event_id)
  {
    int i, r;

  case MQTT_EVENT_ERROR:
    Logger.Info("MQTT event MQTT_EVENT_ERROR");
    break;
  case MQTT_EVENT_CONNECTED:
    Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

    r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
    if (r == -1)
    {
      Logger.Error("Could not subscribe for cloud-to-device messages.");
    }
    else
    {
      Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
    }

    break;
  case MQTT_EVENT_DISCONNECTED:
    Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_SUBSCRIBED:
    Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
    break;
  case MQTT_EVENT_PUBLISHED:
    Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
    break;
  case MQTT_EVENT_DATA:
    Logger.Info("MQTT event MQTT_EVENT_DATA");

    // Define separate buffers for topic and data
    char topic_buffer[INCOMING_DATA_BUFFER_SIZE];
    char data_buffer[INCOMING_DATA_BUFFER_SIZE];

    // Copy the topic into topic_buffer
    for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
    {
      topic_buffer[i] = event->topic[i];
    }
    topic_buffer[i] = '\0'; // Null-terminate the topic string
    Logger.Info("Topic: " + String(topic_buffer));

    if (String(topic_buffer) == AZURE_SUB_TOPIC)
    {

      // Check if the payload is empty
      if (event->data_len == 0)
      {
        Logger.Error("Payload is empty");
        data_buffer[0] = '\0'; // Set data_buffer to an empty string
      }
      else
      {
        // Copy the data into data_buffer
        for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
        {
          data_buffer[i] = event->data[i];
        }
        data_buffer[i] = '\0'; // Null-terminate the data string
      }
      Logger.Info("Data: " + String(data_buffer));

      // Parse the JSON payload
      DeserializationError error = deserializeJson(AzurejsonDoc, data_buffer);

      if (error)
      {
        Logger.Error("Failed to parse JSON: " + String(error.c_str()));
      }
      else
      {
        String SM_ID;
        String Message_Title;

        // Extract values by key
        if (AzurejsonDoc.containsKey("MeterID"))
        {
          SM_ID = AzurejsonDoc["MeterID"].as<String>();

          if (debug)
            Logger.Info(SM_ID);

          if (SM_ID == String(meter_ID))
          {
            if (AzurejsonDoc.containsKey("MessageTitle"))
            {
              Message_Title = AzurejsonDoc["MessageTitle"].as<String>();

              message_title = Message_Title;

              if (debug)
                Logger.Info(Message_Title);

              if (String(Message_Title) == "Calibration") // checking message title for command
              {
                if (debug)
                  Serial.println("Calibration Message");

                String CT_count = AzurejsonDoc["Number_of_CTs"].as<String>();
                String CT1_string = AzurejsonDoc["CT1_ratio"].as<String>();
                String CT2_string = AzurejsonDoc["CT2_ratio"].as<String>();
                String CT3_string = AzurejsonDoc["CT3_ratio"].as<String>();

                number_of_CTs = CT_count.toInt();
                Ratio_CT1 = CT1_string.toInt();
                Ratio_CT2 = CT2_string.toInt();
                Ratio_CT3 = CT3_string.toInt();
                meter_status = "Accepted";
                message_title = "Calibration response";
              }
            }
          }
        }
      }
    }

    break;
  case MQTT_EVENT_BEFORE_CONNECT:
    Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
    break;
  default:
    Logger.Error("MQTT event UNKNOWN");
    break;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#else  // ESP_ARDUINO_VERSION_MAJOR
  return ESP_OK;
#endif // ESP_ARDUINO_VERSION_MAJOR
}

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &AZUREclient,
          az_span_create((uint8_t *)host, strlen(host)),
          az_span_create((uint8_t *)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &AZUREclient, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &AZUREclient, mqtt_username, sizeofarray(mqtt_username), 0)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }
#endif

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  mqtt_config.broker.address.uri = mqtt_broker_uri;
  mqtt_config.broker.address.port = mqtt_port;
  mqtt_config.credentials.client_id = mqtt_client_id;
  mqtt_config.credentials.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  LogInfo("MQTT client using X509 Certificate authentication");
  mqtt_config.credentials.authentication.certificate = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.credentials.authentication.certificate_len = (size_t)sizeof(IOT_CONFIG_DEVICE_CERT);
  mqtt_config.credentials.authentication.key = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
  mqtt_config.credentials.authentication.key_len = (size_t)sizeof(IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY);
#else // Using SAS key
  mqtt_config.credentials.authentication.password = (const char *)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.session.keepalive = 30;
  mqtt_config.session.disable_clean_session = 0;
  mqtt_config.network.disable_auto_reconnect = false;
  mqtt_config.broker.verification.certificate = (const char *)ca_pem;
  mqtt_config.broker.verification.certificate_len = (size_t)ca_pem_len;
#else // ESP_ARDUINO_VERSION_MAJOR
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char *)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char *)ca_pem;
#endif // ESP_ARDUINO_VERSION_MAJOR

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
#endif // ESP_ARDUINO_VERSION_MAJOR

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

static void establishConnection()
{
  initializeIoTHubClient();
  (void)initializeMqttClient();
}

// **************** SETUP ****************
void setup()
{
  // Stabalise
  delay(1000);

  // Initialise UART
  Serial.begin(115200);                 // 115200
  preferences.begin("settings", false); // Namespace "settings", RW mode

  // Register WiFi event handlers
  WiFi.onEvent(onClientConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(onClientDisconnected, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

  // Start the server and manage it
  startServer();
  manageServer(CAPTIVE_PORTAL_TIMEOUT);

  String stored_meter_ID1 = preferences.getString("Meter_ID1", "");
  String stored_meter_ID2 = preferences.getString("Meter_ID2", "");

  	// Reading and concatenating the stored device_key:
	String stored_device_key1 = preferences.getString("Device_Key1", "");
	String stored_device_key2 = preferences.getString("Device_Key2", "");
	String stored_device_key3 = preferences.getString("Device_Key3", "");
	String stored_device_key4 = preferences.getString("Device_Key4", "");
	device_key = stored_device_key1 + stored_device_key2 + stored_device_key3 + stored_device_key4;

  meter_ID = stored_meter_ID1 + stored_meter_ID2;
  mqtt_host_ip = preferences.getString("mqtt_host_ip", "");
  hardware_version = preferences.getString("hw_version", "");
  SSID = preferences.getString("wifi_ssid", "");
  Pass = preferences.getString("wifi_pass", "");

  initWiFi();

  mqtt_server = mqtt_host_ip.c_str();
  MAC_ADDRESS = WiFi.macAddress();

  Serial.println(mqtt_host_ip);
  Serial.println(device_key);
  Serial.println(hardware_version);

  char *device_id = strdup(meter_ID.c_str());
  AZURE_SUB_TOPIC = "devices/" + String(device_id) + "/messages/devicebound";

  strcpy(IOT_CONFIG_DEVICE_KEY, device_key.c_str());

  // Azure IOT setup
  establishConnection();

  // Initialize and get the time
  configTime(0, 0, ntpServer);
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  printLocalTime();

  // IPEM Board Port Configuration
  ConfigureBoard();

  // Check DCV_IN
  CheckDCVINVoltage();

// ****************  Initialise the ATM90E3x & Pass related calibrations to its library ****************

// LineFreq = lineFreq, PGAGain = pgagain, VoltageGainX = ugain, CurrentGainCT1 = igainA, CurrentGainCT2 = igainB, CurrentGainCT3 = igainC, CurrentGainCTN = igainN
#if ATM90DEVICE == ATM90E32_DEVICE && ATM_SPLITPHASE == true // Not Fully Tested.
// the 2nd (B) current channel is not used with the split phase meter
#if ATM_SINGLEVOLTAGE == true
  eic.begin(LineFreq, PGAGain, VoltageGain1, 0, 0, CurrentGainCT1, 0, CurrentGainCT2);
#else
  eic.begin(LineFreq, PGAGain, VoltageGain1, VoltageGain2, VoltageGain3, CurrentGainCT1, 0, CurrentGainCT2);
#endif
#endif

#if ATM90DEVICE == ATM90E32_DEVICE && ATM_SPLITPHASE == false
#if ATM_SINGLEVOLTAGE == true
  // eic.begin(LineFreq, PGAGain, VoltageGain1, 0, 0, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3);
  eic.begin(LineFreq, PGAGain, VoltageGain1, VoltageGain1, VoltageGain1, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3);
#else
  eic.begin(LineFreq, PGAGain, VoltageGain1, VoltageGain2, VoltageGain3, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3);
#endif
#endif

#if ATM90DEVICE == ATM90E36_DEVICE && ATM_SPLITPHASE == false
#if ATM_SINGLEVOLTAGE == true
  // eic.begin(LineFreq, PGAGain, VoltageGain1, 0, 0, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3, CurrentGainCTN);
  eic.begin(LineFreq, PGAGain, VoltageGain1, VoltageGain1, VoltageGain1, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3, CurrentGainCTN);
#else
  eic.begin(LineFreq, PGAGain, VoltageGain1, VoltageGain2, VoltageGain3, CurrentGainCT1, CurrentGainCT2, CurrentGainCT3, CurrentGainCTN);
#endif
#endif

  Serial.println("IPEM Hardware Setup and Power-Up Test - Board Initialized");

  delay(1000);

  Serial.println("");

  DisplayRegisters(); // Display once

} // Setup

// **************** LOOP ****************
void loop() // Future - ISRs To Be Added
{
  // wm.process();
  repeatedCall();

  if (WiFi.status() != WL_CONNECTED)
  {
    initWiFi();
  }
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }

  printLocalTime();

  // Updating MQTT with response
  if (message_title == "Calibration response")
  {
    if (debug)
      Serial.println("Response");

    String Calibrationresponse_payload = "{ \"Timestamp\": \"" + ntp_time + "\", \"MeterID\": \"" + meter_ID + "\",  \"MessageTitle\": \"" + message_title + "\", \"Status\": \"" + meter_status + "\"}";

    if (debug)
    {
      Logger.Info("Sending Calibration response message...");
      Serial.println(Calibrationresponse_payload);
    }

    strcpy(telemetry_topic, AZURE_SUB_TOPIC.c_str()); // Correct

    if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
            &AZUREclient, 0, telemetry_topic, sizeof(telemetry_topic), 0)))
    {
      if (debug)
      {
        Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
      }
      return;
    }

    if (esp_mqtt_client_publish(
            mqtt_client,
            telemetry_topic,
            (const char *)Calibrationresponse_payload.c_str(),
            Calibrationresponse_payload.length(),
            MQTT_QOS1,
            DO_NOT_RETAIN_MSG) == 0)
    {
      if (debug)
      {
        Logger.Error("Failed publishing response message");
      }
    }
    else
    {
      if (debug)
      {
        Logger.Info("Response message published successfully");
      }
    }
    message_title = "";
    // ch_status = "";
  }

  // SM_readings
  unsigned long currentMillis_mqttUpdate = millis(); // store the current time
  if (currentMillis_mqttUpdate - previousMillis_mqttUpdate >= period_mqttUpdate)
  {                                                       // check if 10000ms passed
    previousMillis_mqttUpdate = currentMillis_mqttUpdate; // save the last time mqtt was updated

    printLocalTime();
    String reading_payload;

    if (debug)
      Serial.println("SM Readings Sent");

    StaticJsonDocument<600> esp_data;

    // ----------------------------------------RANDOM VALUES FOR SMART METER
    if (meter_ID == "Meter002")
    {
      float ct1 = random(18, 25) + (float)random(0, 100) / 100;
      float ct2 = random(18, 25) + (float)random(0, 100) / 100;
      float ct3 = random(18, 25) + (float)random(0, 100) / 100;

      float volt = 240.0;

      float instantaneous_consumption = (float)((ct1 + ct2 + ct3) * volt) / 1000;
      if (debug)
        Serial.println(instantaneous_consumption, 4);

      message_title = "Smart Meter Reading";

      reading_payload = "{ \"Timestamp\": \"" + ntp_time + "\", \"MeterID\": \"" + meter_ID + "\", \"MessageTitle\": \"" + message_title + "\", \"CT1current\": \"" + String(ct1) + "\", \"CT2current\": \"" + String(ct2) + "\", \"CT3current\": \"" + String(ct3) + "\", \"kW1\": \"" + String(ct1 * volt / 1000, 4) + "\", \"kW2\": \"" + String(ct2 * volt / 1000, 4) + "\", \"kW3\": \"" + String(ct3 * volt / 1000, 4) + "\", \"BuildingConsumption\": \"" + String(instantaneous_consumption, 4) + "\", \"MacAddress\": \"" + MAC_ADDRESS + "\", \"HardwareVersion\": \"" + hardware_version + "\", \"FirmwareVersion\": \"" + FirmwareVer + "\", \"Method\": \"smart_meter_reading\"}";
    }
    // ----------------------------------------RANDOM VALUES FOR SMART METER

    // ----------------------------------------MEASURED VALUES FOR SMART METER
    else
    {
      float ct1 = eic.GetLineCurrentCT1();
      float ct2 = eic.GetLineCurrentCT2();
      float ct3 = eic.GetLineCurrentCT3();
      float v1 = eic.GetLineVoltage1();
      float v2 = eic.GetLineVoltage2();
      float v3 = eic.GetLineVoltage3();

      if (ct1 < 0.01)
        ct1 = 0;
      if (ct2 < 0.01)
        ct2 = 0;
      if (ct3 < 0.01)
        ct3 = 0;

      float load1, load2, load3;

      load1 = eic.GetActivePowerCT1();
      load2 = eic.GetActivePowerCT2();
      load3 = eic.GetActivePowerCT3();

      if (load1 < 0.001)
        load1 = 0;
      if (load2 < 0.01)
        load2 = 0;
      if (load3 < 0.01)
        load3 = 0;

      float instantaneous_consumption = ((ct1 * v1) + (ct2 * v2) + (ct3 * v3)) / 1000;

      message_title = "Smart Meter Reading";

      reading_payload = "{ \"Timestamp\": \"" + ntp_time + "\", \"MeterID\": \"" + meter_ID + "\", \"MessageTitle\": \"" + message_title + "\", \"CT1current\": \"" + String(ct1, 2) + "\", \"CT2current\": \"" + String(ct2, 2) + "\", \"CT3current\": \"" + String(ct3, 2) + "\", \"kW1\": \"" + String(load1, 2) + "\", \"kW2\": \"" + String(load2, 2) + "\", \"kW3\": \"" + String(load3, 2) + "\", \"BuildingConsumption\": \"" + String(instantaneous_consumption, 2) + "\", \"MacAddress\": \"" + MAC_ADDRESS + "\", \"HardwareVersion\": \"" + hardware_version + "\", \"FirmwareVersion\": \"" + FirmwareVer + "\", \"Method\": \"smart_meter_reading\"}";
    }
    // ----------------------------------------MEASURED VALUES FOR SMART METER

    if (debug)
    {
      Serial.println();
    }

    if (debug)
    {
      Logger.Info("Sending reading message...");
      Serial.println(reading_payload);

      Serial.print("TOPIC: ");
      Serial.println(AZURE_SUB_TOPIC);

      Serial.println();
    }

    strcpy(telemetry_topic, AZURE_SUB_TOPIC.c_str()); // Correct

    if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
            &AZUREclient, 0, telemetry_topic, sizeof(telemetry_topic), 0)))
    {
      if (debug)
      {
        Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
      }
      return;
    }

    if (esp_mqtt_client_publish(
            mqtt_client,
            telemetry_topic,
            (const char *)reading_payload.c_str(),
            reading_payload.length(),
            MQTT_QOS1,
            DO_NOT_RETAIN_MSG) == 0)
    {
      if (debug)
      {
        Logger.Error("Failed publishing response message");
      }
    }
    else
    {
      if (debug)
      {
        Logger.Info("Response message published successfully");
      }
    }
    message_title = "";
  }

  if (debug)
  {
    Serial.println("Voltage 1: " + String(eic.GetLineVoltage1()));
    Serial.println("Voltage 2: " + String(eic.GetLineVoltage2()));
    Serial.println("Voltage 3: " + String(eic.GetLineVoltage3()));

    Serial.println("\nCurrent 1: " + String(eic.GetLineCurrentCT1()));
    Serial.println("Current 2: " + String(eic.GetLineCurrentCT2()));
    Serial.println("Current 3: " + String(eic.GetLineCurrentCT3()));

    Serial.println("---------------------------------");
  }

  // Loop Delay
  delay(LoopDelay * 1000);
  if (digitalRead(0) == LOW)
  {
    delay(3000);
    if (digitalRead(0) == LOW)
    {
      preferences.clear();
      // wm.resetSettings();
      for (int i = 0; i < 100; i++)
      {
        EEPROM.write(i, 0);
        Serial.println(i);
      }
      EEPROM.commit();
      ESP.restart();
    }
  }
} // loop
