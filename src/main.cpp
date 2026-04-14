#include <WiFiManager.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "EmonLib.h"
#include "config.h"

// --- CONFIGURATION ---

#define LED_PIN 2
#define ADC_PIN 34
#define BUZZER_PIN LED_PIN//25
const float CALIBRATION_FACTOR = 2.5;//60.6;

EnergyMonitor emon1;
WebServer server(80);
Preferences preferences;

float currentRMS = 0;
float currentThreshold = 2.0;

// --- SERVER HANDLERS ---

void handleRoot()
{
	// This opens the file from flash and sends it to the browser
	File file = LittleFS.open("/index.html", "r");
	if (!file)
	{
		server.send(404, "text/plain", "File not found. Did you upload the data folder?");
		return;
	}
	server.streamFile(file, "text/html");
	file.close();
}

void handleData()
{
	StaticJsonDocument<100> doc;
	doc["current"] = currentRMS;
	doc["threshold"] = currentThreshold;
	String json;
	serializeJson(doc, json);
	server.send(200, "application/json", json);
}

void handleSet()
{
	if (server.hasArg("val"))
	{
		currentThreshold = server.arg("val").toFloat();
		preferences.putFloat("threshold", currentThreshold);
	}
	server.send(200, "text/plain", "OK");
}

void setup()
{
	pinMode(BUZZER_PIN, OUTPUT);

	// Initialize LittleFS
	if (!LittleFS.begin(true))
	{
		Serial.println("An Error has occurred while mounting LittleFS");
		return;
	}

	WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

	Serial.begin(115200);

	WiFiManager wm;

	wm.preloadWiFi(WiFiSsid, WiFiPassword); // Try to connect to default WiFi
	wm.setConnectTimeout(10);

	// wm.resetSettings(); // Reset settings for testing

	bool res;
	res = wm.autoConnect(apSsid, apPassword); // password protected ap

	if (!res)
	{
		Serial.println("Failed to connect");
		// ESP.restart();
	}
	else
	{
		// if you get here you have connected to the WiFi
		Serial.print("Connected to:");
		Serial.println(wm.getWiFiSSID());
	}

	preferences.begin("power-app", false);
	currentThreshold = preferences.getFloat("threshold", 2.0);

	emon1.current(ADC_PIN, CALIBRATION_FACTOR);

	server.on("/", handleRoot);
	server.on("/data", handleData);
	server.on("/set", handleSet);
	server.begin();
}

void loop()
{
	server.handleClient(); // Handle web requests

	// Measure current
	currentRMS = emon1.calcIrms(1480);
	if (currentRMS < 0.08)
	{
		currentRMS = 0; // Noise floor
	}

	// Buzzer Alert
	if (currentRMS > currentThreshold)
	{
		digitalWrite(BUZZER_PIN, HIGH);
	}
	else
	{
		digitalWrite(BUZZER_PIN, LOW);
	}
}