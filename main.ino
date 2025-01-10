#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

/*
Mqtt error states:
-4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
-3 : MQTT_CONNECTION_LOST - the network connection was broken
-2 : MQTT_CONNECT_FAILED - the network connection failed
-1 : MQTT_DISCONNECTED - the client is disconnected cleanly
0 : MQTT_CONNECTED - the client is connected
1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
*/

// Globals
int8 LED = 0;  // Pin led
int State = 1;  // State in statemachine
String http_data;  // Receive buffer for data from http request
uint16_t publish_register_index;  // Keep track of which register that should be published next cycle
uint16_t u16_buffer[500];  // Data buffer when parsing http_data
uint16_t register_table[500][3];  // Storage for max 500 registers [0] = reg, [1] = value, [2] = ttl

char* internet_ssid = NULL;
char* internet_password = NULL;

const uint16_t DEFAULT_REG_TTL = 3600;  // Seconds before register has expired

const char* wifi_ssids[][2] = {
    {"carma", "sommarbad"},
    {"magolin", "sommarbad"},
    {"Hus", "supermega"},
    {"ost-ng", "magnusmagnus"}};

//const char* topas_ssid = "topas";
//const char* topas_pwd = "tom123456";
const char* topas_ssid = "ost-ng";
const char* topas_pwd = "magnusmagnus";

//const char* topas_url = "http://www.topol.tom/status_read";
const char* topas_url = "http://homeassistant.appelquist.org:8080/status_read";

const char* mqtt_server = "mqtt.appelquist.org";
const uint16_t mqtt_server_port = 1883; 
const char* mqtt_username = "topas";
const char* mqtt_password = "hejatopas";

void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  Serial.println("");
  for (uint8_t t = 4; t > 0; t--) {
    Serial.println("SETUP: Flush");
    Serial.flush();
    delay(1000);
  }

  Serial.println("SETUP: Starting");
  Serial.println("");

  // Clear register table
  // Serial.println("DEBUG: " + String(sizeof(register_table) / sizeof(register_table[0])));
  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    register_table[i][0] = 0;
    register_table[i][1] = 0;
    register_table[i][2] = 0;
  } 

  State = 0;  // Initial state
}

void loop() {

  // Error handler
  if (State == -1) {
    Serial.println("");
    Serial.println("ERROR: Restarting");
    delay(1000);
    void (* re_set)(void) = 0x00;
    re_set();
  }

  State = State + 1;
  //Serial.println("Loop state: " + String(State));

  if (State == 1) {  // Connect Topas wifi
    led_on();
    Serial.println("Wifi: '" + String(topas_ssid) + "'");
    WiFi.begin(topas_ssid, topas_pwd);
    while (WiFi.status() != WL_CONNECTED) {
      led_off();
      delay(250);
      Serial.print(".");
      led_on();
      delay(250);
    }
    Serial.println();

    Serial.print("Wifi: Connected, IP: ");
    Serial.println(WiFi.localIP());
    led_off();
  }


  if (State == 5) {  // Connecto to status_read
    getSaveRegistry(1, 4);
  }

  if (State == 6) {  // Connecto to status_read
    getSaveRegistry(64, 9);
  }

  if (State == 7) {  // Connecto to status_read
    getSaveRegistry(213, 1);
  }

  if (State == 8) {  // Connecto to status_read
    getSaveRegistry(1005, 44);
  }

  if (State == 9) {  // Connecto to status_read
    getSaveRegistry(11050, 18);
  }

  if (State == 10) {  // Connecto to status_read
    getSaveRegistry(10000, 8);
  }

  if (State == 11) {  // Connecto to status_read
    getSaveRegistry(11000, 50);
  }

  if (State == 12) {  // Connecto to status_read
    getSaveRegistry(10058, 1);
  }

  if (State == 20) {  // disconnect wifi topas
    led_on();
    Serial.println("Wifi: Disconnect");
    WiFi.disconnect();
    led_off();
  }

  if (State == 30) {  // Scan wifis

    led_on();
    Serial.println("Wifi: Scan...");
    int numWifi = WiFi.scanNetworks();
    if (numWifi == -1) {
      Serial.println("Wifi: No found");
      State = -1;
      return;
    }
    for (uint16_t thisNet = 0; thisNet < numWifi; thisNet++) {
      Serial.println("Wifi: SSID: '" + WiFi.SSID(thisNet) + "' Signal: " + String(WiFi.RSSI(thisNet)) + " dBm");
      for (uint8_t wifiSsidIndex = 0; wifiSsidIndex < sizeof(wifiSsidIndex/2); wifiSsidIndex++ ) {
        if (String(wifi_ssids[wifiSsidIndex][0]) == WiFi.SSID(thisNet)) {
          internet_ssid = (char*)wifi_ssids[wifiSsidIndex][0];
          internet_password = (char*)wifi_ssids[wifiSsidIndex][1];
        }
      }
    }

    if (internet_ssid == NULL) {
      Serial.println("Wifi: No pwd");
      State = -1;
      return;
    }
    Serial.println("Wifi: Found pwd for '" + String(internet_ssid) + "'");
    led_off();
  } 

  if (State == 31) {  // Connect internet
    led_on();
    Serial.println("Wifi: '" + String(topas_ssid) + "'");
    WiFi.begin(internet_ssid, internet_password);
    while (WiFi.status() != WL_CONNECTED) {
      led_off();
      delay(250);
      Serial.print(".");
      led_on();
      delay(250);
    }
    Serial.println();

    Serial.print("Wifi: Connected, IP: ");
    Serial.println(WiFi.localIP());
    led_off();
  }

  if (State == 32) {  // Connect to mqtt
    led_on();
    Serial.println("MQTT: '" + String(mqtt_server) + "'");
    mqttClient.setServer(mqtt_server, mqtt_server_port);
    //mqttClient.setCallback(mqtt_callback);
    String client_id = "topas-";
    client_id += String(WiFi.macAddress());
    client_id.replace(":", "");
    while (!mqttClient.connected()) {
      led_off();
      if (mqttClient.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
        break;
      } else {
        Serial.println("MQTT: " + String(mqttClient.state()));
        State = -1;
        return;
      }
      Serial.print(".");
      led_on();
      delay(150);
    }
    delay(250);
    Serial.println("MQTT: Connected");

    String topic = getTopic("online");

    mqttClient.publish(topic.c_str(), "1");
  }

  if (State == 35) {  // Clear index
    publish_register_index = 0;
    Serial.println("MQTT: publish values");
  }

  if (State == 36) {  // Publish parsed values, one each cycle
    String parsed_data = "";
    int parsed_value = 0;
    String parsed_value_extra = "";

    for (; publish_register_index < (sizeof(register_table) / sizeof(register_table[0])); publish_register_index++) {
      // uint16_t reg = register_table[publish_register_index][0];
      // uint16_t value = register_table[publish_register_index][1];
      // uint16_t ttl = register_table[publish_register_index][2];
      if (register_table[publish_register_index][0] == 0) {
        continue;
      }

      parsed_value = register_table[publish_register_index][1];

      switch (register_table[publish_register_index][0]) {
        case 11000:
          parsed_data = "current_process_tank_level";
          break;
        case 11001:
          parsed_data = "current_ackumulator_tank_level";
          break;
        case 11002:
          parsed_data = "current_capacity";
          parsed_value = parsed_value / 10;  // in 10th percent
          break;
        case 11003:
          parsed_data = "current_phase";
          switch(register_table[publish_register_index][1]) {
            case 0:
              parsed_value_extra = "Filling process tank";  // Fyllnings av processtank
              break;
            case 1:
              parsed_value_extra = "Sedimentation";  // Sedimentering
              break;
            case 2:
              parsed_value_extra = "Filling decanter";  // Fyller dekanter
              break;
            case 3:
              parsed_value_extra = "Desalination";  // Avslamning
              break;
            case 4:
              parsed_value_extra = "Draining";  // Tömning av renat vatten
              break;
            case 5:
              parsed_value_extra = "Denitri. fulfillment";  // Denitrifiering fyllning av processtank
              break;
            case 6:
              parsed_value_extra = "Denitri. sedimentation";  // Denitrifiering sedimentering
              break;
            case 7:
              parsed_value_extra = "Denitri- recirculation";  // Denitrifiering recirkulation
              break;
            case 8:
              parsed_value_extra = "Triggering";  // Kör
              break;
          }
          break;

        case 64:
          parsed_data = "Pr1";
          parsed_value_extra = stateToText(parsed_value);
          break;
        case 69:
          parsed_data = "Pr3";
          parsed_value_extra = stateToText(parsed_value);
          break;
        case 70:
          parsed_data = "Pr3";
          parsed_value_extra = stateToText(parsed_value);
          break;
        case 71:
          parsed_data = "Pr4";
          parsed_value_extra = stateToText(parsed_value);
          break;
        case 72:
          parsed_data = "Pr5";
          parsed_value_extra = stateToText(parsed_value);
          break;

        case 1006:
          parsed_data = "max_process_tank_level";
          break;
        case 1009:
          parsed_data = "max_ackumulator_tank_level";
          break;

        case 10007:
          led_on();
          parsed_data = "dm_kompressordrift";
          if (parsed_value & 0b0000000000000001) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "pr1";
          if (parsed_value & 0b0000000000000010) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          
          parsed_data = "v1o_process_tank";
          if (parsed_value & 0b0000000000000100) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "v2o_drain_cleaned_water";
          if (parsed_value & 0b0000000000001000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "v3o_denitri_filling";
          if (parsed_value & 0b0000000000010000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "v4o_desalination";  // avslammning
          if (parsed_value & 0b0000000000100000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "pr2_level_process_tank";  // Bräddningsnivå i utjämningstank
          if (parsed_value & 0b0000000001000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "pr3_level_process_tank";  // Bräddningsnivå i utjämningstank
          if (parsed_value & 0b000000010000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "pr4_uv_lamp"; 
          if (parsed_value & 0b000000100000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "pr5_dosing";  // flockningsmedel
          if (parsed_value & 0b0000001000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "input_d1";
          if (parsed_value & 0b000010000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "input_d2";
          if (parsed_value & 0b000100000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "input_d3";
          if (parsed_value & 0b001000000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "input_d4";
          if (parsed_value & 0b010000000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          led_off();
          break;
        
        case 11004:
          parsed_data = "duration_current_phase";  // in minutes
          break;
        case 11005:
          parsed_data = "duration_fill_process_tank";  // in minutes
          break;
        case 11006:
          parsed_data = "duration_sedimentation";  // in minutes
          break;
        case 11007:
          parsed_data = "duration_denitri_filling";  // in minutes
          break;
        case 11008:
          parsed_data = "duration_desalination";  // in minutes
          break;
        case 11009:
          parsed_data = "duration_drain_clean_water";  // in minutes
          break;
        case 11010:
          parsed_data = "duration_denitrify_fill_process_tank";  // in minutes
          break;
        case 11011:
          parsed_data = "duration_denitri_sedimentation";  // in minutes
          break;
        case 11012:
          parsed_data = "duration_denitri_recirculation";  // in minutes
          break;
        case 11025:
          parsed_data = "amount_cleated_water_today";  // in m3
          break;

        case 11047:
          led_on();
          parsed_data = "E107";  // The desludging stage lasts longer than max time (Avslamningsfel processtank)
          if (parsed_value & 0b0000000000000001) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "E108";  // Emergency water level in bio-reactor (Bräddnivå i processtank)
          if (parsed_value & 0b0000000000000010) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          
          parsed_data = "E131";  // Dosing container is nearly empty (Flockningsmedel snart slut)
          if (parsed_value & 0b0000000000000100) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E133";  // Dosing container 2 is nearlt empty (Flockningsmedel tank 2 snart slut)
          if (parsed_value & 0b0000000000001000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          led_off();
          break;

        case 11048:
          led_on();
          parsed_data = "E104";
          if (parsed_value & 0b0000000000000001) {  // Raw sewage water air-lift pump defect or increased wastewater inflow (Pumpfel råvattenpump)
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "E105";  // Long-term increased level in accumulation tank (Högt inflöde)
          if (parsed_value & 0b0000000000000010) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          
          parsed_data = "E106";  //  Denitrification stage lasts longer than max time (Avslamningsfel vid denitrifikation)
          if (parsed_value & 0b0000000000000100) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E130";  // Dosing container is empty (Flockningsmedel behållare tom)
          if (parsed_value & 0b0000000000001000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E132";  // Dosing continer 2 is empty (Flockningsmedel tank 2 behållare tom)
          if (parsed_value & 0b0000000000010000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E110";  // Air pressure drop in ackumulator tank (Utjämningstank tryckfall)
          if (parsed_value & 0b0000000000100000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E111";  //  Air pressure drop in bio-reactor tank (Processtank tryckfall)
          if (parsed_value & 0b0000000001000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E150";  // Critical temperature of control unit (Avloppsreningsverket är överhettat)
          if (parsed_value & 0b000000010000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          led_off();
          break;

        case 11049:
          led_on();
          parsed_data = "E101";  // Emergency water level in accumulation tank (Bräddningsnivå i utjämningstank)
          if (parsed_value & 0b0000000000000001) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "E102";  // Air pressure drop (Tryckfall)
          if (parsed_value & 0b0000000000000010) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          
          parsed_data = "E103";  // The drainage state lasts longer than max time (Pumpfel renvattenpump)
          if (parsed_value & 0b0000000000000100) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }

          parsed_data = "E003";  // License not valid (Licensöverträdelse)
          if (parsed_value & 0b010000000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "E002";  // Electricity power on (Ström återställt)
          if (parsed_value & 0b100000000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          parsed_data = "E001";  // Electricity power off (Strömavbrott)
          if (parsed_value & 0b1000000000000000) {
            mqttClient.publish(getTopic(parsed_data).c_str(), "1");
          } else {
            mqttClient.publish(getTopic(parsed_data).c_str(), "0");
          }
          led_off();
          break;
      }
      if (parsed_data != "") {
        break;
      }
    }

    if (parsed_data != "") {
      led_on();
      String topic = getTopic(parsed_data);

      mqttClient.publish(topic.c_str(), String(parsed_value).c_str());
      Serial.print(".");
      led_off();
      if (parsed_value_extra != "") {
        led_on();
        mqttClient.publish((topic + String("_extra")).c_str(), parsed_value_extra.c_str());
        Serial.print(".");
        led_off();
      }
      publish_register_index = publish_register_index + 1;  // Point to next register next cycle
      State = State - 1;  // Run same state again until all registers are published
    } else {
      Serial.println("");
    }
  }

  if (State == 38) {  // Clear index
    publish_register_index = 0;
    Serial.println("MQTT: publish registers");
  }

  if (State == 39) {  // Publish all registers, one each cycle
    bool found = false;

    for (; publish_register_index < (sizeof(register_table) / sizeof(register_table[0])); publish_register_index++) {
      // uint16_t reg = register_table[publish_register_index][0];
      // uint16_t value = register_table[publish_register_index][1];
      // uint16_t ttl = register_table[publish_register_index][2];
      if (register_table[publish_register_index][0] == 0) {
        continue;
      }
      led_on();
      found = true;
      String topic = getTopic("register/");
      topic += String(register_table[publish_register_index][0]);
      mqttClient.publish(topic.c_str(), String(register_table[publish_register_index][1]).c_str());
      Serial.print(".");
      led_off();
      break;
    }
    if (found) {
      publish_register_index = publish_register_index + 1;  // Point to next register next cycle
      State = State - 1;  // Run same state again until all registers are published
    } else {
      Serial.println("");
    }
  }

  if (State == 45) {  // Disconnect MQTT
    led_on();

    String topic = getTopic("online");

    mqttClient.publish(topic.c_str(), "0");
    delay(250);
    Serial.println("MQTT: Disconnect");
    mqttClient.disconnect();
    led_off();
  }

  if (State == 47) {  // disconnect wifi
    led_on();
    Serial.println("Wifi: Disconnect");
    WiFi.disconnect();
    led_off();
  }

  // Restart, a full cycle is 180 seconds
  if (State == 180) {
    State = 0;
  }

  // Decrease TTL for registers savejd in memory and remove expired registers
  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    // uint16_t reg = register_table[i][0];
    // uint16_t value = register_table[i][1];
    // uint16_t ttl = register_table[i][2];
    if (register_table[i][0] != 0) {
      // Serial.println("REG: " + String(register_table[i][0]) + "=" + String(register_table[i][1]) + " ttl: " + String(register_table[i][2]));
      if (register_table[i][2] > 0) {
        register_table[i][2] = register_table[i][2] - 1;
        if (register_table[i][2] == 0) {
          Serial.println("REG: expire " + String(register_table[i][0]));
          register_table[i][0] = 0;
          register_table[i][1] = 0;
        }
      }
    }
  } 

  delay(1000);
}


//void mqtt_callback(char *topic, byte *payload, unsigned int length) {

bool getSaveRegistry(uint16 e, uint16 t) {
  if (!fetchRegistry(e, t)) {
    Serial.println("Topas: no data");
    return false;
  }
  if (!parseRegister()) {
    Serial.println("Topas: problem parse");
    return false;        
  }
  return true;
}

bool fetchRegistry(uint16 e, uint16 t) {
  HTTPClient http;
  http_data = "";

  if (http.begin(wifiClient, topas_url)) {
    // http.addHeader("Content-Type", "application/x-www-form-urlencoded"); 

    Serial.println("HTTP: POST l=" + String(t) + " i=" + String(e));
    String postData = "l=" + String(t) + "&";
    postData += "p=0&";
    postData += "i=" + String(e) + "&";
    postData += "d=0";

    // Serial.println(postData);
    int httpCode = http.POST(postData);  // Make request
    // Serial.println("HTTP: " + String(httpCode));


    if (httpCode != HTTP_CODE_OK) {
      Serial.println("HTTP: Failed " + http.errorToString(httpCode));
      return false;
    }
  
    http_data = http.getString();    //Get the response payload

    Serial.println("HTTP: len: " + String(http_data.length()) + " data: " + String(http_data));  
    http.end();
  }
  return true;
}

bool parseRegister(void) {
  uint16_t len = 0;

  // Convert hex string to dec array
  for (uint16_t i = 0; i < http_data.length(); i = i + 3) {
    // http_data[i] = " "
    // http_data[i + 1] = "7"
    // http_data[i + 2] = "3"

    // Serial.println("DEBUG " + String(http_data[i]));
    // Serial.println("DEBUG " + String(http_data[i + 1]));
    // Serial.println("DEBUG " + String(http_data[i + 2]));
    String data = String(http_data[i + 1]) + String(http_data[i + 2]);
    int data_dec = strtol(data.c_str(), NULL, 16);
    // Serial.println("DEBUG value: " + String(data_dec));
    u16_buffer[len] = data_dec;
    len = len + 1;
  }

  // u16_buffer is now int array with decimal numbers
  // Serial.println("DEBUG1: " + String(u16_buffer[0]));
  // Serial.println("DEBUG2: " + String(u16_buffer[1]));

  uint16_t i = 0;
  uint16_t reg = 0;
  uint16_t value = 0;
  while (i < (len - 5)) { 
    if (i == 5) {
       reg = (255 & u16_buffer[i]) + (u16_buffer[i + 1] << 8 & 65280);
    } else if (i > 6) {
      value = (255 & u16_buffer[i]) + (u16_buffer[i + 1] << 8 & 65280);
      // Serial.println("DEBUG reg: " + String(reg));
      // Serial.println("DEBUG val: " + String(value));
      saveRegister(reg, value);
      reg += 1;
      i += 1;
    }
    i += 1;
  }
  return true;
}

bool saveRegister(uint16_t reg, uint16_t value) {
  // Serial.println("REG: " + String(reg) + "=" + String(value));
  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    // register_table[i][0] = reg
    // register_table[i][1] = value
    // register_table[i][2] = ttl

    if (register_table[i][0] == 0) {
      register_table[i][0] = reg;
      register_table[i][1] = value;
      register_table[i][2] = DEFAULT_REG_TTL;    
      break;
    }
  } 
  return true;
}

String stateToText(int state) {
  switch(state) {
    case 1:
      return "Manually";
    case 2:
      return "UV-lamp";  
    case 3:
      return "Chemicals";  // Flockningsmedel
    case 4:
      return "Backup blower";  // Kompressordrift sekundär kompressor
    case 5:
      return "Emergency ack. level";  // E101 bräddningsnivå i utjämningstank
    case 6:
      return "Prog. timer 1";
    case 7:
      return "Prog. timer 2";
    case 8:
      return "Prog. timer 3";
    case 9:
      return "Interval timer 1";  
    case 10:
      return "Interval timer 2"; 
    case 11:
      return "Float ACU";  // Flyta i utjämningstank
    case 12:
      return "Float in acc. without emergency";  // Float in Acc. without emergency level
    case 13:
      return "Indication of water discharge";
    case 14:
      return " Chemicals 2";  // Flockningsmedel 2
  }
  return "Unknown";
}

String getTopic(String t) {
  String topic = "topas/";
  topic += String(WiFi.macAddress());
  topic.replace(":", "");
  topic = topic + "/" + t;
  return topic;
}

void led_off() {
  digitalWrite(LED, HIGH);
}
void led_on() {
  digitalWrite(LED, LOW);
}