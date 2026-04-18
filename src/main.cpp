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
const float CALIBRATION_FACTOR = 0.25;//60.6;

EnergyMonitor emon1;
WebServer server(80);
Preferences preferences;

enum SCT013
{
	SCT013005, // 5 A
	SCT013010, // 10 A
	SCT013015, // 15 A
	SCT013020, // 20 A 
	SCT013025, // 25 A
	SCT013030, // 30 A
	SCT013050, // 50 A
	SCT013060, // 60 A
	SCT013000, // 100 A
};

const uint8_t SCT013MAP[] = {5, 10, 15, 20, 25, 30, 50, 60, 100};
const char* SCT013NAME[] = {
	"SCT-013-005", // 5 A
	"SCT-013-010", // 10 A
	"SCT-013-015", // 15 A
	"SCT-013-020", // 20 A
	"SCT-013-025", // 25 A
	"SCT-013-030", // 30 A
	"SCT-013-050", // 50 A
	"SCT-013-060", // 60 A
	"SCT-013-000", // 100 A
};

float currentRMS = 0;
float currentThreshold = 2.0;
float voltage = 230.0;
SCT013 selectedSensor = SCT013010; // Default to 10A sensor

// --- SERVER HANDLERS ---


inline uint8_t getSensorMax(SCT013 sensor)
{
	return SCT013MAP[sensor];
}
inline const char* getSensorName(SCT013 sensor)
{
	return SCT013NAME[sensor];
}


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
	StaticJsonDocument<200> doc;
	doc["current"] = currentRMS;
	doc["threshold"] = currentThreshold;
	doc["sensor"] = selectedSensor;
	doc["sensorName"] = getSensorName(selectedSensor);
	doc["max"] = getSensorMax(selectedSensor);
	doc["voltage"] = voltage;
	String json;
	serializeJson(doc, json);
	server.send(200, "application/json", json);
}

void handleSetThreshold()
{
	if (server.hasArg("thresh"))
	{
		currentThreshold = server.arg("thresh").toFloat();
		preferences.putFloat("threshold", currentThreshold);
	}
	server.send(200, "text/plain", "OK");
}

void handleGetSensors()
{
	StaticJsonDocument<500> doc;
	JsonArray sensors = doc.createNestedArray("sensors");
	
	for (int i = 0; i < 9; i++)
	{
		JsonObject sensor = sensors.createNestedObject();
		sensor["id"] = i;
		sensor["name"] = getSensorName((SCT013)i);
		sensor["max"] = getSensorMax((SCT013)i);
	}
	
	String json;
	serializeJson(doc, json);
	server.send(200, "application/json", json);
}

void handleSetSensor()
{
	if (server.hasArg("id"))
	{
		int sensorId = server.arg("id").toInt();
		if (sensorId >= 0 && sensorId < 9)
		{
			selectedSensor = (SCT013)sensorId;
			preferences.putUChar("sensor", selectedSensor);
			server.send(200, "text/plain", "OK");
			emon1.current(ADC_PIN, getSensorMax(selectedSensor)*CALIBRATION_FACTOR);
		}
		else
		{
			server.send(400, "text/plain", "Invalid sensor ID");
		}
	}
	else
	{
		server.send(400, "text/plain", "Missing sensor ID");
	}
}

void handleSetVoltage()
{
	if (server.hasArg("voltage"))
	{
		voltage = server.arg("voltage").toFloat();
		preferences.putFloat("voltage", voltage);
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
	selectedSensor = (SCT013)preferences.getUChar("sensor", SCT013010);
	voltage = preferences.getFloat("voltage", 230.0);

	emon1.current(ADC_PIN, getSensorMax(selectedSensor)*CALIBRATION_FACTOR);

	server.on("/", handleRoot);
	server.on("/data", handleData);
	server.on("/setThresh", handleSetThreshold);
	server.on("/sensors", handleGetSensors);
	server.on("/setSensor", handleSetSensor);
	server.on("/setVolt", handleSetVoltage);
	server.begin();
}

void loop()
{
	static unsigned long prevMillis = millis();
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
		// dacWrite(BUZZER_PIN, 127);
		// analogWrite(BUZZER_PIN, 127);
		// Blink/beep  alarm
		if(prevMillis + 500 <= millis())
		{
			digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
			prevMillis = millis();
		}
	}
	else
	{
		digitalWrite(BUZZER_PIN, LOW);
	}
}