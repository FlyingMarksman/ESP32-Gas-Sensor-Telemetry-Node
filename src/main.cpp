#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h" // Include the secrets header file for WiFi and MQTT credentials
/*
things to do: 
1.handle sudden wifi disconnection
2. callback
3. add more parameters to the JSON payload (eg alert status)
4. datetime stamps for the MQTT messages (can be done in the cloud with the timestamp of message arrival, but can also be done on the device if we want to have more control over it)
5. callbacks for updates from the cloud (eg change in alert thresholds, or change in transmission interval)
6. average the sensor readings over a short time window to smooth out noise (eg take 5 readings every 2 seconds and average them before transmitting) DONE
7. 1 hour average reading DONE
8. speaker alerts for different levels of CO concentration (eg safe, warning, critical alert)
*/

WiFiClientSecure wifiClientSecure;
PubSubClient mqttClient(wifiClientSecure);

const int sensorPin = 35; // Analog pin connected to the sensor output
const int speakerPin = 25; // Digital pin connected to the speaker for alerts

float criticalThreshold = 10.0; // PPM threshold for critical alert
float warningThreshold = 5.0; // PPM threshold for warning alert
int status = 0; // 1: Safe, 2: Warning, 3: Critical Alert

const char* mqtt_server = SECRET_MQTT_SERVER; 
int mqtt_port = SECRET_MQTT_PORT;
const char* mqtt_user = SECRET_MQTT_USER; 
const char* mqtt_password = SECRET_MQTT_PASS;
const char* ssid = "";
const char* password = "";

int baseline = 39; // Baseline resistance in clean air (R0)
unsigned long lastMsg = 0;
unsigned long interval = 2000; // 2 seconds
bool isWarmedUp = false; // Flag to indicate if the sensor has warmed up
unsigned long warmUpStartTime = 0;
unsigned long warmUpDuration = 60000; // 60 seconds warm-up time for the sensor according to datasheet
WiFiManager wifiManager;


float hourlyTotal = 0; // Total PPM for the current hour

int hourlyCount = 0; // Count of readings for the current hour
unsigned long lastHourCheck = 0; // Timestamp of the last hour check
unsigned long hourInterval = 3600000; // 1 hour in milliseconds



// put function declarations here:
void setup_wifi();
void connectMQTT();
void displayPPM(float ppm);
float calculatePPM(float sensorReading);
void transmitData(float ppm);
void transmissionInterval(unsigned long currentMillis, float ppm);
void setupSpeaker();
void warmUpSensor(unsigned long currentMillis);
float readSensor();
void callback(char* topic, byte* payload, unsigned int length);
void alert(int *status);
void transmitHourlyAverage(unsigned long currentMillis);
void reconnect_wifi();

void setup() {
  Serial.begin(115200);
  // put your setup code here, to run once:
  setup_wifi();
  //setupSpeaker();
  warmUpSensor(millis()); // Start the warm-up timer
  mqttClient.setCallback(callback); // Set the MQTT callback function
  mqttClient.publish("SensorStatus", "Warming up sensor..."); // Publish a status message to MQTT

}

void loop() {
  // put your main code here, to run repeatedly:
  reconnect_wifi(); // Call the callback function to check for incoming MQTT messages (if any)
  if (!mqttClient.connected()) {
    connectMQTT(); // Reconnect to MQTT if the connection is lost
  }
  mqttClient.loop();
  if(isWarmedUp == false){
    warmUpSensor(millis());
    return; // Skip the rest of the loop until the sensor is warmed up
  }
  else{
    if(millis() - lastMsg >= interval){
      float ppm = calculatePPM(readSensor()); // Read the sensor and calculate PPM
      transmitData(ppm);
      lastMsg = millis();// Update the last message timestamp
    }
    transmitHourlyAverage(millis());
  }
   // Check if it's time to transmit data and do so if needed
   alert(&status); // Update the alert status based on the current PPM reading
}

void setupSpeaker() {
  pinMode(speakerPin, OUTPUT);
}


void alert(int *status) {
  // Simple alert mechanism using the speaker
  // You can customize the tones and durations based on the status
  unsigned long currentMillis = millis();
  unsigned long alertInterval = 2000; // Alert every 2 seconds when in warning or critical status
  unsigned long lastAlertTime = 0; // Timestamp of the last alert
  switch (*status) {
    case 1: // Safe
      digitalWrite(speakerPin, LOW); // No tone for safe status
      break;
    case 2: // Warning
      if(currentMillis - lastAlertTime >= alertInterval) {
        digitalWrite(speakerPin, HIGH); // Play a short tone at 2000 Hz
        
      }if(currentMillis - lastAlertTime >= 1000) { // Short pause after the tone
        digitalWrite(speakerPin, LOW); 
        lastAlertTime = currentMillis; // Update the last alert time
      }
      break;
    case 3: // Critical Alert
      if(currentMillis - lastAlertTime >= 500) {
        digitalWrite(speakerPin, HIGH); // Play a short tone at 4000 Hz
        if(currentMillis - lastAlertTime >= 250) { // Short pause after the tone
          digitalWrite(speakerPin, LOW); 
          lastAlertTime = currentMillis; // Update the last alert time
        }
        
      }
      break;
    default:
      noTone(speakerPin); // Stop any tone if status is unknown
      break;
  }
}


void setup_wifi() {
  WiFiManager wifiManager;
  bool res = wifiManager.startConfigPortal("Gas Sensor Set-up");// Start the configuration portal with a custom SSID. The function will block until the user connects and configures the WiFi credentials, or it times out (default is 180 seconds). It returns true if connected to WiFi, false if it timed out.
  if(res){
    Serial.println("Connected to WiFi!");
    wifiManager.stopConfigPortal(); // Stop the configuration portal if it was started
    mqttClient.setClient(wifiClientSecure);
  } else {
    Serial.println("Failed to connect to WiFi and hit timeout");
    // You can choose to reset the ESP or handle it as needed
    esp_restart(); // Restart the ESP to try again
  }
}

void reconnect_wifi(){
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi connection lost. Attempting to reconnect...");
    WiFi.reconnect(); // Attempt to reconnect to the last known WiFi network
    delay(5000); // Wait for a few seconds before checking again
  }
}

void connectMQTT() {
  // Implement MQTT connection logic here
  wifiClientSecure.setInsecure(); // Use with caution: This will accept any certificate, which is not secure. For production, use proper certificate validation.
  mqttClient.setServer(mqtt_server, mqtt_port);

  while(!mqttClient.connected()) {
    Serial.println("Connecting to MQTT...");
    if (mqttClient.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("Connected to MQTT broker!");
      // Subscribe to topics or publish messages as needed
      mqttClient.subscribe("Sensor/Commands"); // Subscribe to a topic for receiving commands (e.g., threshold updates)
    } else {
      Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" trying again in 5 seconds"); 
      delay(5000);
      
    }
  }
  
}


float calculatePPM(float sensorReading){
  float vPin = (sensorReading / 4095.0) * 3.3; // Convert ADC value to voltage 4095 for 12-bit ADC(according to datasheet), 3.3V reference
  
  // 2. Un-scale the Voltage Divider from potentiometer to get the actual sensor voltage
  float vOut = vPin * 1.5; 

  // 3. Divide-by-Zero Safety Net
  if (vOut <= 0) {
    vOut = 0.001; 
  }

  // 4. Calculate Resistance based on the 5V power rail
  float sResistance = 8.33 * ((5.1 - vOut) / vOut);
  
  // 5. Calculate Ratio and PPM
  float ratio = sResistance / baseline;
  float ppm = 1.2 * pow(ratio, -1.35); // CO Power Law with sensitivity factor of 1.2

   // Call the alert function to update the status based on the current PPM reading
   if(ppm < warningThreshold) {
     status = 1;// Safe
   } else if(ppm < criticalThreshold) {
     status = 2; // Warning
   } else {
    status = 3; // Critical Alert
   }
   hourlyTotal += ppm; // Add the current PPM reading to the hourly total
   hourlyCount++; // Increment the count of readings for the current hour
   return ppm;
}

void displayPPM(float ppm){
  Serial.print("CO Concentration: ");
  Serial.print(ppm);
  Serial.println(" ppm");
  
}

void transmitData(float ppm){
  // Documentation: https://arduinojson.org/v7/
  JsonDocument doc; // Create a JSON document
  //JsonDocument alert; // Create a JSON document with a capacity of 256 bytes
  doc["GAS_PPM"] = ppm; // Add the PPM value to the JSON document with a key "GAS_PPM"
  char jsonBuffer[128]; // Create a buffer to hold the serialized JSON // Buffer to hold the alert status string

  // Serialize the alert status JSON document into the buffer
  serializeJson(doc, jsonBuffer); // Serialize the JSON document into the buffer https://arduinojson.org/v7/tutorial/serialization/
  mqttClient.publish("Sensor/Readings", jsonBuffer); // Publish the JSON string to the MQTT topic "Sensor/Readings"
}

void callback(char* topic, byte* payload, unsigned int length) {

  JsonDocument callbackDoc; // Create a JSON document to hold the deserialized data
  DeserializationError error = deserializeJson(callbackDoc, payload, length); // Check for deserialization errors
  if (error) {
    Serial.print("Failed to deserialize JSON payload: ");
    Serial.println(error.c_str());
    return; // Exit the callback if deserialization fails
  }
  if(callbackDoc["newCriticalThreshold"].is<float>()) {
    Serial.print("Received new critical threshold: ");
    Serial.println(callbackDoc["newCriticalThreshold"].as<float>());
    criticalThreshold = callbackDoc["newCriticalThreshold"]; // Extract the new critical threshold value from the JSON document
    Serial.println("Updated critical threshold: " + String(criticalThreshold)); // Print the updated critical threshold to the serial monitor
  }
   if(callbackDoc["newWarningThreshold"].is<float>()) {
    Serial.print("Received new warning threshold: ");
    Serial.println(callbackDoc["newWarningThreshold"].as<float>());
    warningThreshold = callbackDoc["newWarningThreshold"]; // Extract the new warning threshold value from the JSON document
    Serial.println("Updated warning threshold: " + String(warningThreshold)); // Print the updated warning threshold to the serial monitor
  }
 
}

void warmUpSensor(unsigned long currentMillis){
  if (!isWarmedUp) {
     // Publish a status message to MQTT
    if (currentMillis - warmUpStartTime >= warmUpDuration) {
      isWarmedUp = true; // Sensor has warmed up
      Serial.println("Sensor warmed up and ready for accurate readings.");
    }
  }
}

float readSensor(){
  float sensorValue = 0;
   for(int i = 0; i < 5; i++){ // Take 5 readings and average them to smooth out noise
      sensorValue += analogRead(sensorPin);
      delay(40); // Delay between readings (total of 2 seconds for 5 readings)
  }
  return sensorValue/5;
}
void transmitHourlyAverage(unsigned long currentMillis){
  if (currentMillis - lastHourCheck >= hourInterval) {
    lastHourCheck = currentMillis; // Update the last hour check timestamp
    if (hourlyCount > 0) { // Avoid division by zero
      float hourlyAverage = hourlyTotal / hourlyCount; // Calculate the average PPM for the hour
      JsonDocument doc; // Create a JSON document
      doc["Hourly_Average_PPM"] = hourlyAverage; // Add the hourly average PPM to the JSON document
      char jsonBuffer[64]; // Create a buffer to hold the serialized JSON
      serializeJson(doc, jsonBuffer); // Serialize the JSON document into the buffer
      mqttClient.publish("HourlyAverage", jsonBuffer); // Publish the hourly average to the MQTT topic "HourlyAverage"
      hourlyTotal = 0; // Reset the hourly total for the next hour
      hourlyCount = 0; // Reset the count for the next hour
    }
  }
}

