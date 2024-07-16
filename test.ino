#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi credentials (initially empty)
String ssid = "";
String password = "";
bool isConnected = false;

// Keep track of time
unsigned long previousMillis = 0;
const long interval = 30 * 60 * 1000;  // Interval in milliseconds (30 minutes)

// Access Point credentials
const char *ssidAP = "ESP32-Access-Point-EE";
const char *passwordAP = "12345678";

// OpenWeatherMap API key
const char* apiKey = "1421467c096319d5900a0aceeff035b3";

// DHT sensor settings
#define DHTPIN 5
#define DHTTYPE DHT11

BluetoothSerial SerialBT;
WebServer server(80);
DHT dht(DHTPIN, DHTTYPE);

// MQTT settings
const char *mqtt_broker = "test.mosquitto.org";
const char *topicSensor = "ee/ce360/weatherapp/sensor";
const char *topicApi = "ee/ce360/weatherapp/api";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  delay(2000);
  dht.begin();
  SerialBT.begin("ESP32-BT-EE"); // Initialize Bluetooth with a name
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssidAP, passwordAP); // Set up ESP32 as an access point

  // Mount SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
  }
}

void loop() {
  // Check if data is available from Bluetooth and WiFi is not connected
  if (SerialBT.available() >= 2 && !isConnected) {
    connectToWiFi();
  }

  // Handle client requests if connected to WiFi
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
    client.loop(); // Keep the MQTT connection alive
  }

   unsigned long currentMillis = millis();

  // Check if it's time to publish data
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Your data collection and publishing logic here
    float latitude = 38.4004;
    float longitude = 27.1326;

    // Fetch weather data and publish
    String weatherData = fetchWeatherData(latitude, longitude);

    // Parse JSON to extract humidity and temperature
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, weatherData);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    } else {
      float apiHumidity = doc["main"]["humidity"];
      float apiTemperature = doc["main"]["temp"];
      String apiData = "{\"humidity\":" + String(apiHumidity) + ",\"temperature\":" + String(apiTemperature) + "}";
      client.publish(topicApi, apiData.c_str());
    }

    // Read sensor data and publish
    float sensorHumidity = dht.readHumidity();
    float sensorTemperature = dht.readTemperature();
    String sensorData = "{\"humidity\":" + String(sensorHumidity) + ",\"temperature\":" + String(sensorTemperature) + "}";
    client.publish(topicSensor, sensorData.c_str());
  }
}

// Function to fetch weather data from OpenWeatherMap API
String fetchWeatherData(float latitude, float longitude) {
  String serverPath = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(latitude, 4) + "&lon=" + String(longitude, 4) + "&appid=" + String(apiKey) + "&units=metric";

  HTTPClient http;
  http.begin(serverPath);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(response); // Print the raw JSON response
    http.end(); // Close connection
    return response;
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    http.end(); // Close connection
    return "Error fetching weather data";
  }
}

// HTTP handler for root URL
void handleRoot() {
  String path = "/index.html";
  File file = SPIFFS.open(path, "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  String htmlContent = "";
  while (file.available()) {
    htmlContent += char(file.read());
  }

  file.close();
  if (htmlContent.length() == 0) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  server.send(200, "text/html", htmlContent);
}

// HTTP handler to fetch weather data
void handleGetWeather() {
  if (server.hasArg("lat") && server.hasArg("lon")) {
    float latitude = server.arg("lat").toFloat();
    float longitude = server.arg("lon").toFloat();
    String weatherData = fetchWeatherData(latitude, longitude);

    // Parse JSON to extract location name and temperature
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, weatherData);
    
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      server.send(500, "text/plain", "Failed to parse weather data");
      return;
    }

    float humidity = doc["main"]["humidity"];
    float temperature = doc["main"]["temp"];
    String apiData = "{\"humidity\":" + String(humidity) + ",\"temperature\":" + String(temperature) + "}";

    client.publish(topicApi, apiData.c_str());
    
    server.send(200, "application/json", apiData); // Send reduced weather data as JSON
  }
}

void handleGetSensorData() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Creating JSON patterned patterns
  String sensorData = "{\"humidity\":" + String(humidity) + ",\"temperature\":" + String(temperature) + "}";

  // Send JSON data to client
  server.send(200, "application/json", sensorData);
}


// Function to send sensor data to another server
void sendSensorData(float humidity, float temperature) {
  HTTPClient http;
  const char* serverUrl = "http://192.168.4.1:80/updateSensorData";

  String url = String(serverUrl) + "?humidity=" + String(humidity) + "&temperature=" + String(temperature);
  http.begin(url);

  int httpResponseCode = http.GET();
  if (httpResponseCode == HTTP_CODE_OK) {
    Serial.println("Sensor data sent successfully");
  } else {
    Serial.print("Error sending sensor data. HTTP error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// MQTT callback function
void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
  }
  Serial.println();
  Serial.println("-----------------------");
}

// Function to connect to WiFi
void connectToWiFi() {
  // Read SSID and password from Bluetooth
  String data = SerialBT.readStringUntil('\n');

  // Split received data into SSID and password
  int separatorIndex = data.indexOf(':');
  if (separatorIndex != -1) {
    ssid = data.substring(0, separatorIndex);
    password = data.substring(separatorIndex + 1);
    
    // Connect to WiFi
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(1000);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected!");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      isConnected = true; // Set flag to indicate connection

      // Set up MQTT connection
      client.setServer(mqtt_broker, mqtt_port);
      client.setCallback(callback);
      while (!client.connected()) {
        String client_id = "esp32-client-";
        client_id += String(WiFi.macAddress());
        Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
        if (client.connect(client_id.c_str())) {
          Serial.println("Public EMQX MQTT broker connected");
          client.subscribe(topicApi);
          client.subscribe(topicSensor);
        } else {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
        }
      }
    } else {
      Serial.println("Failed to connect to WiFi!");
    }
  } else {
    Serial.println("Invalid data format!");
  }

  // Set up server routes
  server.on("/", handleRoot);
  server.on("/getWeather", HTTP_GET, handleGetWeather);
  server.on("/getSensorData", HTTP_GET, handleGetSensorData);

  server.begin();
}
