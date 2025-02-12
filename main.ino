#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager 
#include <StateMachine.h>   // https://github.com/jrullan/StateMachine
#include <neotimer.h>       // https://github.com/jrullan/neotimer
// #include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <TelnetStream.h>
#include <Print.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <HTTPClient.h>     // HttpClient
#include <Freenove_WS2812_Lib_for_ESP32.h>  // RGB driver

/* ----- Defines ----- */
#define PIN_LED 2
#define PIN_BUTTON 0
#define LED_RGB 16
#define OTA_PORT 3232
#define TELNET_PORT 23

#define LED_COLOR_WHITE 255, 255, 255
#define LED_COLOR_RED 255, 0, 0
#define LED_COLOR_OFF 0, 0, 0
#define LED_COLOR_BLUE 0, 0, 255
#define LED_COLOR_MAGENTA 255, 0, 255
#define LED_COLOR_YELLOW 255, 255, 0
#define LED_COLOR_VIOLET 238, 130, 238

#define LED_COLOR_GREEN_1 0, 50, 0
#define LED_COLOR_GREEN_2 0, 128, 0
#define LED_COLOR_GREEN_3 50, 205, 50
#define LED_COLOR_GREEN_4 0, 255, 0

#define LED_COLOR_BLUE_1 0, 0, 50
#define LED_COLOR_BLUE_2 0, 0, 128
#define LED_COLOR_BLUE_3 50, 50, 205
#define LED_COLOR_BLUE_4 0, 0, 255

#define LED_COLOR_YELLOW_1 100, 100, 0
#define LED_COLOR_YELLOW_2 128, 128, 0
#define LED_COLOR_YELLOW_3 205, 205, 50
#define LED_COLOR_YELLOW_4 255, 255, 0

#define LED_COLOR_ORANGE 255, 165, 0
#define LED_COLOR_PINK 255, 105, 180
#define LED_COLOR_RED_3 205, 50, 50

/* ----- EEPROM map ----- */
struct eeprom_storage {
  char wifi_ssid[32];
  char wifi_password[32];

  char topas_ssid[32];  
  char topas_password[32];
  char topas_url[90];

  char mqtt_server[30];
  char mqtt_username[30];
  char mqtt_password[30];
  uint16_t mqtt_server_port;

  uint16_t poll_interval;

} eeprom;

/* ----- Declare static variables ----- */

class MySerial: public Print {
    using Print::Print;   // Inheriting constructors
    public: 
    size_t write(uint8_t val) override;   // Overriding base functionality
    size_t write(const uint8_t *buffer, size_t size) override;   // Overriding base functionality
};
MySerial logger;

Freenove_ESP32_WS2812 rgb_driver = Freenove_ESP32_WS2812(1, LED_RGB, 0, TYPE_GRB);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

WiFiManager wifiManager;
WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server");
WiFiManagerParameter custom_mqtt_username("mqtt_username", "MQTT username");
WiFiManagerParameter custom_mqtt_password("mqtt_password", "MQTT password");
WiFiManagerParameter custom_mqtt_server_port("mqtt_server_port", "MQTT server port");
WiFiManagerParameter custom_topas_ssid("topas_ssid", "Topas SSID");
WiFiManagerParameter custom_topas_password("topas_password", "Topas password (tom123456)");
WiFiManagerParameter custom_topas_url("topas_url", "Topas URL (http://www.topol.tom/status_read)");
WiFiManagerParameter custom_poll_interval("poll_interval", "Topas poll interval");

Neotimer timeoutTimer = Neotimer();  // General timeout timer used in states
Neotimer registerTableTimer = Neotimer();  // Timer that keeps track of TTL in register_table

/* Declare state machine */
StateMachine machine = StateMachine();
State* State_CheckButtonOnBoot = machine.addState([](){});
State* State_Configure = machine.addState(&startConfigurationManager);
State* State_SetupWifi = machine.addState(&setupWifi);
State* State_SetupStream = machine.addState(&setupStream);
State* State_SetupMqtt = machine.addState(&setupMqtt);
State* State_SendMqttReportRegisters = machine.addState(&sendMqttReportRegisters);
State* State_SendMqttReportParsed = machine.addState(&sendMqttReportParsed);
State* State_SetupOta = machine.addState(&setupOta);

State* State_SetupWifiTopas = machine.addState(&setupWifiTopas);
State* State_DisconnectWifi = machine.addState(&disconnectWifi);
State* State_DisconnectWifiTopas = machine.addState(&disconnectWifiTopas);
State* State_ReadTopas = machine.addState(&readTopas);

State* State_RegisterTableTTL = machine.addState(&registerTableTTL);

State* State_SetupIdle = machine.addState(&setupIdle);
State* State_Idle = machine.addState(&idle);
State* State_EepromSave = machine.addState(&saveEeprom);
State* State_Error = machine.addState(&error);

/* Static variables */
uint16_t tick;  // Increased every loop, every ~10 mS
uint16_t tick_250;  // Increased every ~250 mS
uint16_t stateCounter;  // generic counter that can be used in states
uint16_t stateCounterMax;  // dynamic max before break

/* Topas variables */
const uint16_t DEFAULT_REG_TTL = 3600;  // Seconds before register has expired
const uint16_t MAX_PARSED_BUFFER = 20;
uint16_t register_table[500][3];  // Storage for max 500 registers [0] = reg, [1] = value, [2] = ttl

struct parsed_register {
  String value;
  String data;
};

/* ----- Setup ----- */
void setup() {

  /* Setup transitions for states */
  State_CheckButtonOnBoot->addTransition(&checkButtonPressed, State_Configure);
  State_CheckButtonOnBoot->addTransition(&checkButtonNotPressed, State_SetupWifi);

  State_SetupWifi->addTransition(&checkSetupWifi, State_SetupStream);
  State_SetupWifi->addTransition(&waitTimeout, State_Error);

  State_SetupStream->addTransition(&waitTimeout, State_SetupOta);
  State_SetupStream->addTransition(&waitStreamConnected, State_SetupOta);

  State_SetupOta->addTransition(&waitTimeout, State_SetupMqtt);

  /* Report mqtt */
  State_SetupMqtt->addTransition(&checkSetupMqtt, State_SendMqttReportParsed);
  State_SetupMqtt->addTransition(&waitTimeout, State_Error);
  State_SendMqttReportParsed->addTransition(&checkMqttReportRegisters, State_SendMqttReportRegisters);
  State_SendMqttReportRegisters->addTransition(&checkMqttReportRegisters, State_SetupIdle);

  /* Idle */
  State_SetupIdle->addTransition(&checkTrue, State_Idle);
  State_Idle->addTransition(&checkMqttNotConnected, State_SetupMqtt);  // if connection is lost, reconnect
  State_Idle->addTransition(&waitRegisterTableTimeout, State_RegisterTableTTL);  // every second, maintain register_table TTL
  State_Idle->addTransition(&waitTimeout, State_DisconnectWifi);

  State_RegisterTableTTL->addTransition(&checkTrue, State_Idle);

  State_DisconnectWifi->addTransition(&waitTimeout, State_SetupWifiTopas);

  State_SetupWifiTopas->addTransition(&checkSetupWifi, State_ReadTopas);
  State_SetupWifiTopas->addTransition(&waitTimeout, State_Error);

  State_ReadTopas->addTransition(&waitTimeout, State_DisconnectWifiTopas);
  State_ReadTopas->addTransition(&checkStateCounter, State_DisconnectWifiTopas);
  State_DisconnectWifiTopas->addTransition(&waitTimeout, State_SetupWifi);

  State_EepromSave->addTransition(&waitTimeout, State_Error);

  /* Initialize pins */
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP); 
  led(false);

  rgb_driver.begin();
  rgb_driver.setBrightness(10);
  ledRGB(LED_COLOR_OFF);

  /* Read storage from EEPROM */
  EEPROM.begin(sizeof(eeprom));
  EEPROM.get(0, eeprom);

  /* Clear register_table */
  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    register_table[i][0] = 0;
    register_table[i][1] = 0;
    register_table[i][2] = 0;
  }

  /* Setup serial port and flush */
  Serial.begin(115200);
  Serial.println("SETUP: Flush");
  Serial.flush();
  Serial.println("SETUP: Starting");
}

/* ----- Main loop ----- */
void loop() {
  ArduinoOTA.handle();
  machine.run();
  processCommands();
  mqttClient.loop();

  tick++;
  if (tick % 25 == 0){
    tick_250++;
  }
  delay(10);
}

/* --- States ---- */

void error(void) {
  /*  Error state, restart */
  ledRGB(LED_COLOR_RED);
  logger.println("Error state, restarting");
  ESP.restart();
}

/* Setup wifi */
void setupWifi(void) {
  if (machine.executeOnce) {
    logger.println("Wifi: SSID '" + String(eeprom.wifi_ssid) + "'");
    logger.println("Wifi: Pwd '" + String(eeprom.wifi_password) + "'");
    logger.println("Wifi: Connecting");

    WiFi.begin(eeprom.wifi_ssid, eeprom.wifi_password);
    
    timeoutTimer.set(30000);  // timeout if not connected within this time
    timeoutTimer.start();
  }
  rgbBlink(LED_COLOR_GREEN_4);
}

/* Setup wifi Topas */
void setupWifiTopas(void) {
  if (machine.executeOnce) {
    logger.println("Topas: SSID '" + String(eeprom.topas_ssid) + "'");
    logger.println("Topas: Pwd '" + String(eeprom.topas_password) + "'");
    logger.println("Topas: Connecting Wifi");

    WiFi.begin(eeprom.topas_ssid, eeprom.topas_password);
    
    timeoutTimer.set(30000);  // timeout if not connected within this time
    timeoutTimer.start();
  }
  rgbBlink(LED_COLOR_GREEN_4);
}

void disconnectWifi(void) {
  if (machine.executeOnce) {
    timeoutTimer.set(5000);
    timeoutTimer.start();
    stateCounter = 0;
    return;
  }

  switch(stateCounter) {
    case 100:
      {
        if (mqttClient.connected()) {
          String topic = getTopic("online");
          mqttClient.publish(topic.c_str(), "0");
        }
      }
      break;

    case 200:
      {
        if (mqttClient.connected()) {
          mqttClient.disconnect();
        }
      }
      break;

    case 300:  
      logger.println("Wifi: Disconnecting");
      break;

    case 250:
      TelnetStream.flush();
      TelnetStream.stop();
      TelnetStream.end();
      break;

    case 350:
      WiFi.disconnect();
  }
  rgbBlink(LED_COLOR_GREEN_1);
  stateCounter++;
}

void disconnectWifiTopas(void) {
  if (machine.executeOnce) {
    timeoutTimer.set(3000);
    timeoutTimer.start();
    logger.println("Wifi: Disconnecting");
    WiFi.disconnect();
    return;
  }
  rgbBlink(LED_COLOR_GREEN_1);
}

void readTopas(void) {
  if (machine.executeOnce) {
    logger.println("Topas: Read data");
    timeoutTimer.set(60000);
    timeoutTimer.start();
    stateCounter = 0;
    stateCounterMax = 1100;
    return;
  }

  rgbBlink(LED_COLOR_PINK);

  // Don't do all requests at the same cycle (each cycle is ~10 ms)
  switch (stateCounter) {
    case 300:
      getSaveRegistry(1, 4);
      break;
    case 400:
      getSaveRegistry(64, 9);
      break;
    case 500:
      getSaveRegistry(213, 1);
      break;
    case 600:
      getSaveRegistry(1005, 44);
      break;
    case 700:
      getSaveRegistry(11050, 18);
      break;
    case 800:
      getSaveRegistry(10000, 8);
      break;
    case 900:
      getSaveRegistry(11000, 50);
      break;
    case 1000:
      getSaveRegistry(10058, 1);
      break;
  }
  stateCounter++;
}

void registerTableTTL(void) {
  // Decrease TTL for registers saved in memory and remove expired registers
  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    // uint16_t reg = register_table[i][0];
    // uint16_t value = register_table[i][1];
    // uint16_t ttl = register_table[i][2];
    if (register_table[i][0] != 0) {
      //logger.println("REG: " + String(register_table[i][0]) + "=" + String(register_table[i][1]) + " ttl: " + String(register_table[i][2]));
      if (register_table[i][2] > 0) {
        register_table[i][2] = register_table[i][2] - 1;
        if (register_table[i][2] == 0) {
          logger.println("REG: expire " + String(register_table[i][0]));
          register_table[i][0] = 0;
          register_table[i][1] = 0;
        }
      }
    }
  }
  return;
}

void setupStream(void) {
  if (machine.executeOnce) {
    TelnetStream.begin(TELNET_PORT);
    logger.println("Stream: Setup");
    logger.println("Stream: Waiting for client");
    // dummy wait so client has time to connect
    timeoutTimer.set(5000);
    timeoutTimer.start();
  }
  rgbBlink(LED_COLOR_BLUE_1);
}

void setupOta(void) {
  if (machine.executeOnce) {
    logger.println("OTA: Setup");
    ArduinoOTA.setPort(OTA_PORT);
    // ArduinoOTA.setHostname("myesp32");
    //ArduinoOTA.setPassword("");
  
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }
      
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      logger.println("OTA: Start updating " + type);
      ledRGB(LED_COLOR_RED);
      })
      .onEnd([]() {
        logger.println("");
        logger.println("OTA: End");
        TelnetStream.flush();
        TelnetStream.stop();
        TelnetStream.end();
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        if (progress % 1000 == 0) {
          logger.println("OTA: Progress: " + String(progress) + "/" + String(total));
        }
      })
      .onError([](ota_error_t error) {
        logger.printf("OTA: Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
          logger.println("OTA: Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
          logger.println("OTA: Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
          logger.println("OTA: Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
          logger.println("OTA: Receive Failed");
        } else if (error == OTA_END_ERROR) {
          logger.println("OTA: End Failed");
        }
    });
    ArduinoOTA.begin();

    timeoutTimer.set(3000);  // wait 3 seconds before continue
    timeoutTimer.start();
  }
  rgbBlink(LED_COLOR_BLUE_3);
}

/* Setup mqtt */
void setupMqtt(void) {
  if (machine.executeOnce) {
    logger.println("MQTT: '" + String(eeprom.mqtt_server) + "'");
    mqttClient.setServer(eeprom.mqtt_server, eeprom.mqtt_server_port);
    //mqttClient.setCallback(mqttCallback);

    timeoutTimer.set(30000);
    timeoutTimer.start();
  }

  rgbBlink(LED_COLOR_YELLOW_2);
}

uint16_t publish_register_index;  // static to keep track of published registers

void sendMqttReportRegisters(void) {
  if (machine.executeOnce) {
    String topic = getTopic("online");
    mqttClient.publish(topic.c_str(), "1");

    logger.println("MQTT: Send registers");
    publish_register_index = 0;
  }
  rgbBlink(LED_COLOR_MAGENTA);

  if (tick % 10 == 0) {  // every 100 mS

    // uint16_t reg = register_table[publish_register_index][0];
    // uint16_t value = register_table[publish_register_index][1];
    // uint16_t ttl = register_table[publish_register_index][2];

    for (; publish_register_index < (sizeof(register_table) / sizeof(register_table[0])); publish_register_index++) {
      if (register_table[publish_register_index][0] == 0) {
        continue;  // if empty, get next
      }
      led(true);
      String topic = getTopic("register/");
      topic += String(register_table[publish_register_index][0]);
      mqttClient.publish(topic.c_str(), String(register_table[publish_register_index][1]).c_str());
      led(false);
      publish_register_index++;
      break;
    } 
  }
}

void sendMqttReportParsed(void) {
  if (machine.executeOnce) {
    String topic = getTopic("online");
    mqttClient.publish(topic.c_str(), "1");

    logger.println("MQTT: Send parsed");
    publish_register_index = 0;
  }
  rgbBlink(LED_COLOR_MAGENTA);

  if (tick % 10 == 0) {  // every 100 mS

    // uint16_t reg = register_table[publish_register_index][0];
    // uint16_t value = register_table[publish_register_index][1];
    // uint16_t ttl = register_table[publish_register_index][2];


    for (; publish_register_index < (sizeof(register_table) / sizeof(register_table[0])); publish_register_index++) {
      if (register_table[publish_register_index][0] == 0) {
        continue;  // if empty, get next
      }

      struct parsed_register list_of_parsed_registers[MAX_PARSED_BUFFER];  // max nbr of entries
      parseTopasRegiser(register_table[publish_register_index][0], register_table[publish_register_index][1], list_of_parsed_registers);

      for (uint8_t i = 0; i<MAX_PARSED_BUFFER; i++) {
        if (list_of_parsed_registers[i].data == "") {
          continue;
        }
        led(true);
        String topic = getTopic(list_of_parsed_registers[i].data);
        logger.println("MQTT: " + topic + " = " + String(list_of_parsed_registers[i].value));
        mqttClient.publish(topic.c_str(), String(list_of_parsed_registers[i].value).c_str());
        led(false);
      }
      publish_register_index++;
      break;
    } 
  }
}

bool checkMqttReportRegisters(void) {
  return publish_register_index >= (sizeof(register_table) / sizeof(register_table[0]));
}

bool checkStateCounter(void) {
  return stateCounter == stateCounterMax;
}

void startConfigurationManager(void) {
  ledRGB(LED_COLOR_WHITE);
  wifiManager.setTitle("Topas2mqtt");

  custom_mqtt_server.setValue(eeprom.mqtt_server, 30);
  wifiManager.addParameter(&custom_mqtt_server);
  custom_mqtt_username.setValue(eeprom.mqtt_username, 30);
  wifiManager.addParameter(&custom_mqtt_username);
  custom_mqtt_password.setValue(eeprom.mqtt_password, 30);
  wifiManager.addParameter(&custom_mqtt_password);
  custom_mqtt_server_port.setValue(String(eeprom.mqtt_server_port).c_str(), 5);
  wifiManager.addParameter(&custom_mqtt_server_port);

  custom_topas_ssid.setValue(eeprom.topas_ssid, 32);
  wifiManager.addParameter(&custom_topas_ssid);
  custom_topas_password.setValue(eeprom.topas_password, 32);
  wifiManager.addParameter(&custom_topas_password);
  custom_topas_url.setValue(eeprom.topas_url, 90);
  wifiManager.addParameter(&custom_topas_url);

  custom_poll_interval.setValue(String(eeprom.poll_interval).c_str(), 5);
  wifiManager.addParameter(&custom_poll_interval);

  wifiManager.setDebugOutput(true, WM_DEBUG_NOTIFY );
  logger.println("CONFIG: starting portal");

  WiFi.begin();
  String ap_name = "configure-";
  ap_name += String(WiFi.macAddress());
  ap_name.replace(":", "");

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.startConfigPortal(ap_name.c_str())) {
    logger.println("SETUP: failed");
    machine.transitionTo(State_Error);
  }
  ledRGB(LED_COLOR_OFF);
}

/* ----- Transitions ----- */

bool checkSetupWifi(void) {
  if (WiFi.status() == WL_CONNECTED) {
    logger.print("Wifi: Connected, IP: ");
    logger.println(WiFi.localIP());
    ledRGB(LED_COLOR_OFF);
    return true;
  }
  return false;
}

bool checkSetupMqtt(void) {
  if (mqttClient.connected()) {
    logger.println("MQTT: Connected");
    // mqttClient.subscribe(getTopic("command").c_str());
    ledRGB(LED_COLOR_OFF);
    return true;
  }

  if (tick % 500 == 0) {  // reconnect every 5:th second
    String client_id = "topas-";
    client_id += String(WiFi.macAddress());
    client_id.replace(":", "");

    if (!mqttClient.connect(client_id.c_str(), eeprom.mqtt_username, eeprom.mqtt_password)) {
      logger.println("MQTT: " + String(mqttClient.state()));
    }
  }
  return false;
}

bool checkMqttNotConnected(void) {
  if (mqttClient.connected()) {
    return false;
  } 
  logger.println("MQTT: Lost connection");
  return true;
}

bool checkTrue(void) {
  return true;
}

bool waitTimeout(void) {
  if (timeoutTimer.done()) {
    logger.println("Timer: timeout");
    ledRGB(LED_COLOR_OFF);
    return true;
  }
  return false;
}

bool waitRegisterTableTimeout(void) {
  if (registerTableTimer.done()) {
    return true;
  }
  return false;
}

bool checkButtonPressed(void) {
  return digitalRead(PIN_BUTTON) == 0;  // 0 = pressed
}
bool checkButtonNotPressed(void) {
  return digitalRead(PIN_BUTTON) == 1;  // 1 = not pressed
}

bool waitStreamConnected(void) {
  return TelnetStream.available();  // wait until client connects
}

/* ----- Callbacks ----- */
void saveConfigCallback () {
  logger.println("CONFIG: Save config");

  logger.println("CONFIG: ssid " + wifiManager.getWiFiSSID());
  memset(eeprom.wifi_ssid, 0, sizeof(eeprom.wifi_ssid)); 
  strncpy(eeprom.wifi_ssid, wifiManager.getWiFiSSID().c_str(), sizeof(eeprom.wifi_ssid));

  logger.println("CONFIG: pwd " + wifiManager.getWiFiPass());
  memset(eeprom.wifi_password, 0, sizeof(eeprom.wifi_password)); 
  strncpy(eeprom.wifi_password, wifiManager.getWiFiPass().c_str(), sizeof(eeprom.wifi_password));

  logger.println("CONFIG: MQTT server " + String(custom_mqtt_server.getValue()));
  memset(eeprom.mqtt_server, 0, sizeof(eeprom.mqtt_server)); 
  strncpy(eeprom.mqtt_server, custom_mqtt_server.getValue(), sizeof(eeprom.mqtt_server));

  logger.println("CONFIG: MQTT username " + String(custom_mqtt_username.getValue()));
  memset(eeprom.mqtt_username, 0, sizeof(eeprom.mqtt_username)); 
  strncpy(eeprom.mqtt_username, custom_mqtt_username.getValue(), sizeof(eeprom.mqtt_username));

  logger.println("CONFIG: MQTT password " + String(custom_mqtt_password.getValue()));
  memset(eeprom.mqtt_password, 0, sizeof(eeprom.mqtt_password)); 
  strncpy(eeprom.mqtt_password, custom_mqtt_password.getValue(), sizeof(eeprom.mqtt_password));

  logger.println("CONFIG: MQTT port " + String(custom_mqtt_server_port.getValue()));
  eeprom.mqtt_server_port = String(custom_mqtt_server_port.getValue()).toInt();

  logger.println("CONFIG: topas ssid " + String(custom_topas_ssid.getValue()));
  memset(eeprom.topas_ssid, 0, sizeof(eeprom.topas_ssid)); 
  strncpy(eeprom.topas_ssid, custom_topas_ssid.getValue(), sizeof(eeprom.topas_ssid));

  logger.println("CONFIG: topas password " + String(custom_topas_password.getValue()));
  memset(eeprom.topas_password, 0, sizeof(eeprom.topas_password)); 
  strncpy(eeprom.topas_password, custom_topas_password.getValue(), sizeof(eeprom.topas_password));

  logger.println("CONFIG: topas url " + String(custom_topas_url.getValue()));
  memset(eeprom.topas_url, 0, sizeof(eeprom.topas_url)); 
  strncpy(eeprom.topas_url, custom_topas_url.getValue(), sizeof(eeprom.topas_url));

  logger.println("CONFIG: Poll interval  " + String(custom_poll_interval.getValue()));
  eeprom.poll_interval = String(custom_poll_interval.getValue()).toInt();

  machine.transitionTo(State_EepromSave);
}

void saveEeprom(void) {
  if (machine.executeOnce) {
    EEPROM.put(0, eeprom);
    delay(100);
    EEPROM.commit();
    logger.println("EEPROM: Saved");
    timeoutTimer.set(3000);
    timeoutTimer.start();
  }
  rgbBlinkSlow(LED_COLOR_RED_3);
}

void setupIdle(void) {
  logger.println("IDLE");
  timeoutTimer.set(eeprom.poll_interval * 1000);  // wait until connect to Topas
  timeoutTimer.start();
}

void idle(void) {
  if (machine.executeOnce) {
    registerTableTimer.set(1000);
    registerTableTimer.start();
  }

  rgbBlinkSlow(LED_COLOR_GREEN_4);
}

void processCommands(void) {
  switch (TelnetStream.read()) {
    case 'r':
      logger.println("Restarting");
      delay(100);
      TelnetStream.flush();
      TelnetStream.stop();
      TelnetStream.end();
      delay(100);
      ESP.restart();
      break;
    case 'i':
      logger.println("Idle state");
      machine.transitionTo(State_Idle);
      break;
    case 's':
      logger.println("--- Status ----");
      logger.print("IP: ");
      logger.println(WiFi.localIP());
      logger.println("SSID: " + String(eeprom.wifi_ssid));

      logger.println("MQTT-server: " + String(eeprom.mqtt_server));
      logger.println("Topas SSID: " + String(eeprom.topas_ssid));
      logger.println("Topas password: " + String(eeprom.topas_password));
      logger.println("Topas URL: " + String(eeprom.topas_url));
      logger.println("Poll interval: " + String(eeprom.poll_interval));

      logger.println("--- Register ---");
      for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
        if (register_table[i][0] != 0) {
          logger.println("Reg: " + String(register_table[i][0]) + "=" + String(register_table[i][1]) + " TTL: " + String(register_table[i][2]));
        }
      }

      break;
    case 'q':
      logger.println("Bye");
      TelnetStream.flush();
      TelnetStream.stop();
      break;
    }
}

void led(bool state) {
  if (state) {
    digitalWrite(PIN_LED, HIGH);
  } else {
    digitalWrite(PIN_LED, LOW);
  }
}

void ledRGB(uint8_t r, uint8_t g, uint8_t b) {
  rgb_driver.setLedColorData(0, r, g, b);
        rgb_driver.show();   // Send color data to LED
}

void ledBlink(void) {
  led(tick_250 %2 == 0);  // Blink LED every 250 mS
}

void rgbBlink(uint8_t r, uint8_t g, uint8_t b) {
  if (tick_250 %2 == 0) {  // Blink LED every 250 mS
    ledRGB(r, g, b);
  } else {
    ledRGB(0, 0, 0);
  }
}

void rgbBlinkSlow(uint8_t r, uint8_t g, uint8_t b) {
  if (tick % 500 == 0 || tick % 500 == 1) {  
    ledRGB(r, g, b);
  } else {
    ledRGB(0, 0, 0);
  }
}

/* Override serial print */
size_t MySerial::write(uint8_t val) {
    TelnetStream.write(val);
    return Serial.write(val);
}
size_t MySerial::write(const uint8_t *buffer, size_t size) {
    TelnetStream.write(buffer, size);
    return Serial.write(buffer, size);
}

/* Generate topic for MQTT */
String getTopic(String t) {
  String topic = "topas/";
  topic += String(WiFi.macAddress());
  topic.replace(":", "");
  topic = topic + "/" + t;
  return topic;
}

/* ----- Topas functions ----- */

bool getSaveRegistry(uint16_t e, uint16_t t) {
  String http_data;  // Receive buffer for data from http request

  http_data = fetchRegistry(e, t);
  if (http_data.length() == 0) {
    logger.println("Topas: no data");
    return false;
  }
  if (!parseRegister(http_data)) {
    logger.println("Topas: problem parse");
    return false;
  }
  return true;
}

String fetchRegistry(uint16_t e, uint16_t t) {
  HTTPClient http;
  String http_data = "";

  if (http.begin(wifiClient, eeprom.topas_url)) {
    // http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    logger.println(eeprom.topas_url);
    logger.println("HTTP: POST l=" + String(t) + " i=" + String(e));
    String postData = "l=" + String(t) + "&";
    postData += "p=0&";
    postData += "i=" + String(e) + "&";
    postData += "d=0";

    logger.println(postData);
    int httpCode = http.POST(postData);  // Make request
    logger.println("HTTP: " + String(httpCode));


    if (httpCode != HTTP_CODE_OK) {
      logger.println("HTTP: Failed " + http.errorToString(httpCode));
      return http_data;
    }

    http_data = http.getString();    //Get the response payload

    logger.println("HTTP: len: " + String(http_data.length()) + " data: " + String(http_data));
    http.end();
  }
  return http_data;
}

bool parseRegister(String http_data) {
  uint16_t len = 0;
  uint16_t u16_buffer[500];  // Data buffer when parsing http_data

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
  //logger.println("REG: " + String(reg) + "=" + String(value));

  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    if (register_table[i][0] == reg) {
      // already an entry in table, set new value and ttl
      register_table[i][1] = value;
      register_table[i][2] = DEFAULT_REG_TTL;
      return true;
    }
  }

  for (uint16_t i = 0; i < (sizeof(register_table) / sizeof(register_table[0])); i++) {
    if (register_table[i][0] == 0) {
      // first empty
      register_table[i][0] = reg;
      register_table[i][1] = value;
      register_table[i][2] = DEFAULT_REG_TTL;
      break;
    }
  }
  return true;
}

void parseTopasRegiser(uint16_t reg, int16_t value, struct parsed_register list_of_parsed_registers[]) {

  list_of_parsed_registers[0].value = String(value).toInt();
  list_of_parsed_registers[0].data = "";

  switch (reg) {
    case 11000:
      list_of_parsed_registers[0].data = "current_process_tank_level";
      break;
    case 11001:
      list_of_parsed_registers[0].data = "current_ackumulator_tank_level";
      break;
    case 11002:
      list_of_parsed_registers[0].data = "current_capacity";
      list_of_parsed_registers[0].value = String(value / 10).toInt();  // in 10th percent
      break;
    case 11003:
      list_of_parsed_registers[0].data = "current_phase";
      switch(value) {
        case 0:
          list_of_parsed_registers[0].value = "Filling process tank";  // Fyllnings av processtank
          break;
        case 1:
          list_of_parsed_registers[0].value = "Sedimentation";  // Sedimentering
          break;
        case 2:
          list_of_parsed_registers[0].value = "Filling decanter";  // Fyller dekanter
          break;
        case 3:
          list_of_parsed_registers[0].value = "Desalination";  // Avslamning
          break;
        case 4:
          list_of_parsed_registers[0].value = "Draining";  // Tömning av renat vatten
          break;
        case 5:
          list_of_parsed_registers[0].value = "Denitri. fulfillment";  // Denitrifiering fyllning av processtank
          break;
        case 6:
          list_of_parsed_registers[0].value = "Denitri. sedimentation";  // Denitrifiering sedimentering
          break;
        case 7:
          list_of_parsed_registers[0].value = "Denitri- recirculation";  // Denitrifiering recirkulation
          break;
        case 8:
          list_of_parsed_registers[0].value = "Triggering";  // Kör
          break;
      }
      break;

    case 64:
      list_of_parsed_registers[0].data = "Pr1";
      list_of_parsed_registers[0].value = stateToText(value);
      break;
    case 69:
      list_of_parsed_registers[0].data = "Pr3";
      list_of_parsed_registers[0].value = stateToText(value);
      break;
    case 70:
      list_of_parsed_registers[0].data = "Pr3";
      list_of_parsed_registers[0].value = stateToText(value);
      break;
    case 71:
      list_of_parsed_registers[0].data = "Pr4";
      list_of_parsed_registers[0].value = stateToText(value);
      break;
    case 72:
      list_of_parsed_registers[0].data = "Pr5";
      list_of_parsed_registers[0].value = stateToText(value);
      break;

    case 1006:
      list_of_parsed_registers[0].data = "max_process_tank_level";
      break;
    case 1009:
      list_of_parsed_registers[0].data = "max_ackumulator_tank_level";
      break;

    case 10007:
      {
      list_of_parsed_registers[0].data = "dm_kompressordrift";
      if (value & 0b0000000000000001) {
        list_of_parsed_registers[0].value = "1";
      } else {
        list_of_parsed_registers[0].value = "0";
      }

      list_of_parsed_registers[1].data = "pr1";
      if (value & 0b0000000000000010) {
        list_of_parsed_registers[1].value = "1";
      } else {
        list_of_parsed_registers[1].value = "0";
      }

      list_of_parsed_registers[2].data = "v1o_process_tank";
      if (value & 0b0000000000000100) {
        list_of_parsed_registers[2].value = "1";
      } else {
        list_of_parsed_registers[2].value = "0";
      }

      list_of_parsed_registers[3].data = "v2o_drain_cleaned_water";
      if (value & 0b0000000000001000) {
        list_of_parsed_registers[3].value = "1";
      } else {
        list_of_parsed_registers[3].value = "0";
      }

      list_of_parsed_registers[4].data = "v3o_denitri_filling";
      if (value & 0b0000000000010000) {
        list_of_parsed_registers[4].value = "1";
      } else {
        list_of_parsed_registers[4].value = "0";
      }

      list_of_parsed_registers[5].data = "v4o_desalination";  // avslammning
      if (value & 0b0000000000100000) {
        list_of_parsed_registers[5].value = "1";
      } else {
        list_of_parsed_registers[5].value = "0";
      }

      list_of_parsed_registers[6].data = "pr2_level_process_tank";  //  Bräddningsnivå i utjämningstank
      if (value & 0b0000000001000000) {
        list_of_parsed_registers[6].value = "1";
      } else {
        list_of_parsed_registers[6].value = "0";
      }

      list_of_parsed_registers[7].data = "pr3_level_process_tank";   // Bräddningsnivå i utjämningstank
      if (value & 0b000000010000000) {
        list_of_parsed_registers[7].value = "1";
      } else {
        list_of_parsed_registers[7].value = "0";
      }

      list_of_parsed_registers[8].data = "pr4_uv_lamp";
      if (value & 0b000000100000000) {
        list_of_parsed_registers[8].value = "1";
      } else {
        list_of_parsed_registers[8].value = "0";
      }

      list_of_parsed_registers[9].data = "pr5_dosing"; // flockningsmedel
      if (value & 0b0000001000000000) {
        list_of_parsed_registers[9].value = "1";
      } else {
        list_of_parsed_registers[9].value = "0";
      }

      list_of_parsed_registers[10].data = "input_d1";
      if (value & 0b000010000000000) {
        list_of_parsed_registers[10].value = "1";
      } else {
        list_of_parsed_registers[10].value = "0";
      }

      list_of_parsed_registers[11].data = "input_d2";
      if (value & 0b000100000000000) {
        list_of_parsed_registers[11].value = "1";
      } else {
        list_of_parsed_registers[11].value = "0";
      }

      list_of_parsed_registers[12].data = "input_d3";
      if (value & 0b001000000000000) {
        list_of_parsed_registers[12].value = "1";
      } else {
        list_of_parsed_registers[12].value = "0";
      }

      list_of_parsed_registers[13].data = "input_d4";
      if (value & 0b010000000000000) {
        list_of_parsed_registers[13].value = "1";
      } else {
        list_of_parsed_registers[13].value = "0";
      }
      } 
      break;

    case 11004:
      list_of_parsed_registers[0].data = "duration_current_phase";  // in minutes
      break;
    case 11005:
      list_of_parsed_registers[0].data = "duration_fill_process_tank";  // in minutes
      break;
    case 11006:
      list_of_parsed_registers[0].data = "duration_sedimentation";  // in minutes
      break;
    case 11007:
      list_of_parsed_registers[0].data = "duration_denitri_filling";  // in minutes
      break;
    case 11008:
      list_of_parsed_registers[0].data = "duration_desalination";  // in minutes
      break;
    case 11009:
      list_of_parsed_registers[0].data = "duration_drain_clean_water";  // in minutes
      break;
    case 11010:
      list_of_parsed_registers[0].data = "duration_denitrify_fill_process_tank";  // in minutes
      break;
    case 11011:
      list_of_parsed_registers[0].data = "duration_denitri_sedimentation";  // in minutes
      break;
    case 11012:
      list_of_parsed_registers[0].data = "duration_denitri_recirculation";  // in minutes
      break;
    case 11025:
      list_of_parsed_registers[0].data = "amount_cleated_water_today";  // in m3
      break;

    case 11047:
      list_of_parsed_registers[0].data = "E107"; // The desludging stage lasts longer than max time (Avslamningsfel processtank)
      if (value & 0b0000000000000001) {
        list_of_parsed_registers[0].value = "1";
      } else {
        list_of_parsed_registers[0].value = "0";
      }
      list_of_parsed_registers[1].data = "E108";  // Emergency water level in bio-reactor (Bräddnivå i processtank)
      if (value & 0b0000000000000010) {
        list_of_parsed_registers[1].value = "1";
      } else {
        list_of_parsed_registers[1].value = "0";
      }

      list_of_parsed_registers[2].data = "E131";  // Dosing container is nearly empty (Flockningsmedel snart slut)
      if (value & 0b0000000000000100) {
        list_of_parsed_registers[2].value = "1";
      } else {
        list_of_parsed_registers[2].value = "0";
      }

      list_of_parsed_registers[3].data = "E133";  // Dosing container 2 is nearlt empty (Flockningsmedel tank 2 snart slut)
      if (value & 0b0000000000001000) {
        list_of_parsed_registers[3].value = "1";
      } else {
        list_of_parsed_registers[3].value = "0";
      }
      break;

    case 11048:
      list_of_parsed_registers[0].data = "E104";
      if (value & 0b0000000000000001) {  // Raw sewage water air-lift pump defect or increased wastewater inflow (Pumpfel råvattenpump)
        list_of_parsed_registers[0].value = "1";
      } else {
        list_of_parsed_registers[0].value = "0";
      }
      list_of_parsed_registers[1].data = "E105";  // Long-term increased level in accumulation tank (Högt inflöde)
      if (value & 0b0000000000000010) {
        list_of_parsed_registers[1].value = "1";
      } else {
        list_of_parsed_registers[1].value = "0";
      }

      list_of_parsed_registers[2].data = "E106";  //  Denitrification stage lasts longer than max time (Avslamningsfel vid denitrifikation)
      if (value & 0b0000000000000100) {
        list_of_parsed_registers[2].value = "1";
      } else {
        list_of_parsed_registers[2].value = "0";
      }

      list_of_parsed_registers[3].data = "E130";  // Dosing container is empty (Flockningsmedel behållare tom)
      if (value & 0b0000000000001000) {
        list_of_parsed_registers[3].value = "1";
      } else {
        list_of_parsed_registers[3].value = "0";
      }

      list_of_parsed_registers[4].data = "E132";  // Dosing continer 2 is empty (Flockningsmedel tank 2 behållare tom)
      if (value & 0b0000000000010000) {
        list_of_parsed_registers[4].value = "1";
      } else {
        list_of_parsed_registers[4].value = "0";
      }

      list_of_parsed_registers[5].data = "E110";  // Air pressure drop in ackumulator tank (Utjämningstank tryckfall)
      if (value & 0b0000000000100000) {
        list_of_parsed_registers[5].value = "1";
      } else {
        list_of_parsed_registers[5].value = "0";
      }

      list_of_parsed_registers[6].data = "E111";  //  Air pressure drop in bio-reactor tank (Processtank tryckfall)
      if (value & 0b0000000001000000) {
        list_of_parsed_registers[6].value = "1";
      } else {
        list_of_parsed_registers[6].value = "0";
      }

      list_of_parsed_registers[7].data = "E150";  // Critical temperature of control unit (Avloppsreningsverket är överhettat)
      if (value & 0b000000010000000) {
        list_of_parsed_registers[7].value = "1";
      } else {
        list_of_parsed_registers[7].value = "0";
      }
      break;

    case 11049:
      list_of_parsed_registers[0].data = "E101";  // Emergency water level in accumulation tank (Bräddningsnivå i utjämningstank)
      if (value & 0b0000000000000001) {
        list_of_parsed_registers[0].value = "1";
      } else {
        list_of_parsed_registers[0].value = "0";
      }
      list_of_parsed_registers[1].data = "E102";  // Air pressure drop (Tryckfall)
      if (value & 0b0000000000000010) {
        list_of_parsed_registers[1].value = "1";
      } else {
        list_of_parsed_registers[1].value = "0";
      }

      list_of_parsed_registers[2].data = "E103";  // The drainage state lasts longer than max time (Pumpfel renvattenpump)
      if (value & 0b0000000000000100) {
        list_of_parsed_registers[2].value = "1";
      } else {
        list_of_parsed_registers[2].value = "0";
      }

      list_of_parsed_registers[3].data = "E003";  // License not valid (Licensöverträdelse)
      if (value & 0b010000000000000) {
        list_of_parsed_registers[3].value = "1";
      } else {
        list_of_parsed_registers[3].value = "0";
      }
      list_of_parsed_registers[4].data = "E002";  // Electricity power on (Ström återställt)
      if (value & 0b100000000000000) {
        list_of_parsed_registers[4].value = "1";
      } else {
        list_of_parsed_registers[4].value = "0";
      }
      list_of_parsed_registers[5].data = "E001";  // Electricity power off (Strömavbrott)
      if (value & 0b1000000000000000) {
        list_of_parsed_registers[5].value = "1";
      } else {
        list_of_parsed_registers[5].value = "0";
      }
      break;
  }
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

