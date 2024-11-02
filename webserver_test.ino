
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include "webConfigWifiAndMQTT.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>

// set the settings for your WiFi ACCESS POINT
struct WifiSettings {
  char apName[20] = "ESP8266-config";
  char apPassword[20];
  char SSID[20];
  char oldSSID[20];
  char password[20];
  char oldPassword[20];
  char wm_old_wifi_ssid_identifier[9] = "old_ssid";
  char wm_old_wifi_password_identifier[18] = "old_wifi_password";
  char wm_wifi_ssid_identifier[5] = "ssid";
  char wm_wifi_password_identifier[14] = "wifi_password";
};

struct MQTTSettings {
  char clientId[20];
  char hostname[40];
  char port[6];
  char user[20];
  char password[20];
  char wm_mqtt_client_id_identifier[15] = "mqtt_client_id";
  char wm_mqtt_hostname_identifier[14] = "mqtt_hostname";
  char wm_mqtt_port_identifier[10] = "mqtt_port";
  char wm_mqtt_user_identifier[10] = "mqtt_user";
  char wm_mqtt_password_identifier[14] = "mqtt_password";
};

// save config to file
bool shouldSaveConfig = false;
bool checkWiFiSaveInFile = false;
WiFiClient espClient;
WifiSettings wifisettings;
MQTTSettings mqttsettings;
PubSubClient mqtt_client(espClient);
ESP8266WebServer webServer(80);
WiFiManager wifiManager;

void readSettingsFromConfig() {
  //clean FS for testing
  //  SPIFFS.format();
  // Đọc cấu hình từ SPIFFS
  Serial.println("Mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("Mounted file system");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("Reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file");
        size_t size = configFile.size();

        if (size > 0)  // Kiểm tra file không rỗng
        {
          std::unique_ptr<char[]> buf(new char[size + 1]);  // +1 cho ký tự null terminator
          configFile.readBytes(buf.get(), size);
          buf[size] = '\0';  // Đảm bảo chuỗi kết thúc với '\0'

          StaticJsonDocument<1024> doc;
          DeserializationError error = deserializeJson(doc, buf.get());
          if (error) {
            Serial.println(F("Failed to load JSON config"));
          } else {
            Serial.println("\nParsed JSON successfully");

            // Đọc thông tin từ JSON và sao chép vào cấu trúc
            strncpy(mqttsettings.clientId, doc[mqttsettings.wm_mqtt_client_id_identifier], sizeof(mqttsettings.clientId));
            strncpy(mqttsettings.hostname, doc[mqttsettings.wm_mqtt_hostname_identifier], sizeof(mqttsettings.hostname));
            strncpy(mqttsettings.port, doc[mqttsettings.wm_mqtt_port_identifier], sizeof(mqttsettings.port));
            strncpy(mqttsettings.user, doc[mqttsettings.wm_mqtt_user_identifier], sizeof(mqttsettings.user));
            strncpy(mqttsettings.password, doc[mqttsettings.wm_mqtt_password_identifier], sizeof(mqttsettings.password));
            strcpy(wifisettings.SSID, doc[wifisettings.wm_wifi_ssid_identifier]);
            strcpy(wifisettings.password, doc[wifisettings.wm_wifi_password_identifier]);
            strcpy(wifisettings.oldSSID, doc[wifisettings.wm_old_wifi_ssid_identifier]);
            strcpy(wifisettings.oldPassword, doc[wifisettings.wm_old_wifi_password_identifier]);
            Serial.print("Do dai cua ssid trong doc: ");
            Serial.println(strlen(wifisettings.SSID));
            Serial.print("Do dai cua wifi passs trong doc: ");
            Serial.println(strlen(wifisettings.SSID));
            if (strlen(wifisettings.SSID) != 0 && strlen(wifisettings.SSID) != 0) {
              checkWiFiSaveInFile = true;
            }
          }
        } else {
          Serial.println("Config file is empty");
        }

        configFile.close();  // Đóng file
      } else {
        Serial.println("Failed to open config file");
      }
    } else {
      Serial.println("Config file does not exist");
    }
  } else {
    Serial.println("Failed to mount file system");
  }
}


void mainpage() {
  String htmlWebSite = MainPage;
  webServer.send(200, "text/html", htmlWebSite);
}
void initializeMqttClient() {
  Serial.println("Local IP:");
  Serial.println(WiFi.localIP());

  mqtt_client.setServer(mqttsettings.hostname, atoi(mqttsettings.port));
  mqtt_client.setCallback(mqttCallBack);
}

void mqttCallBack(char *topic, byte *payload, unsigned int length) {
  String reveiceMessage = "";
  Serial.print("Message received on topic: ");
  Serial.print(topic);
  Serial.print(".   Message: ");
  for (int i = 0; i < length; i++) {
    reveiceMessage += (char)payload[i];
  }
  Serial.println();
  Serial.print(reveiceMessage);
  Serial.println();
  Serial.println("------------------------");
}
unsigned long lastReconnectAttempt = 0;
void connectToMQTTBroker() {
  if (!mqtt_client.connected()) {
    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      String client_id = "esp8266-client-" + String(WiFi.macAddress());
      Serial.printf("Connecting to MQTT Broker as %s...... \n", client_id.c_str());
      if (mqtt_client.connect(client_id.c_str(), mqttsettings.user, mqttsettings.password)) {
        Serial.println("Connected to MQTT Broker");
        mqtt_client.subscribe("study/ac");
        lastReconnectAttempt = 0;
      } else {
        Serial.println("Connection failed");
        Serial.println(mqtt_client.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
      }
    }
  } else {
    mqtt_client.loop();
  }
}

void saveConfigCallBack() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
void createAccessPoint() {
  // set timeout until configuration
   wifiManager.setConfigPortalTimeout(360);

  // set minium quality of signal
  //  wifiManager.setMinimumSignalQuality();
  if (!wifiManager.autoConnect(wifisettings.apName)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }else{
    strcpy(wifisettings.SSID,wifiManager.getWiFiSSID().c_str());
    strcpy(wifisettings.password,wifiManager.getWiFiPass().c_str());
  }
  Serial.println("WiFi connected!");
}

void initializeWiFiManager() {
  WiFiManagerParameter custom_mqtt_client("client_id", "MQTT Client ID", mqttsettings.clientId, 20);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttsettings.hostname, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqttsettings.port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqttsettings.user, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqttsettings.password, 20);

  // Reset wifi setting for testing
  //  wifiManager.resetSettings();

  wifiManager.setSaveConfigCallback(saveConfigCallBack);

  // add all your parameters here
  wifiManager.addParameter(&custom_mqtt_client);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);

  //start auto create an Access point in the first time programmed
  createAccessPoint();

  strcpy(mqttsettings.clientId, custom_mqtt_client.getValue());
  strcpy(mqttsettings.hostname, custom_mqtt_server.getValue());
  strcpy(mqttsettings.port, custom_mqtt_port.getValue());
  strcpy(mqttsettings.user, custom_mqtt_user.getValue());
  strcpy(mqttsettings.password, custom_mqtt_pass.getValue());
}

void saveConfig() {
  Serial.println("\n----------------------------------------");
  Serial.println("Saving config");
  StaticJsonDocument<1024> doc;
  doc[mqttsettings.wm_mqtt_client_id_identifier] = mqttsettings.clientId;
  doc[mqttsettings.wm_mqtt_hostname_identifier] = mqttsettings.hostname;
  doc[mqttsettings.wm_mqtt_port_identifier] = mqttsettings.port;
  doc[mqttsettings.wm_mqtt_user_identifier] = mqttsettings.user;
  doc[mqttsettings.wm_mqtt_password_identifier] = mqttsettings.password;
  doc[wifisettings.wm_wifi_ssid_identifier] = wifisettings.SSID;
  doc[wifisettings.wm_wifi_password_identifier] = wifisettings.password;
  doc[wifisettings.wm_old_wifi_ssid_identifier] = wifisettings.oldSSID;
  doc[wifisettings.wm_old_wifi_password_identifier] = wifisettings.oldPassword;

  Serial.print("Doc client id: ");
  Serial.println(doc[mqttsettings.wm_mqtt_client_id_identifier].as<String>());
  Serial.print("DOC hostname: ");
  Serial.println(doc[mqttsettings.wm_mqtt_hostname_identifier].as<String>());
  Serial.print("DOC mqtt user: ");
  Serial.println(doc[mqttsettings.wm_mqtt_user_identifier].as<String>());
  Serial.print("DOC mqtt password: ");
  Serial.println(doc[mqttsettings.wm_mqtt_password_identifier].as<String>());
  Serial.print("DOC ssid user: ");
  Serial.println(doc[wifisettings.wm_wifi_ssid_identifier].as<String>());
  Serial.print("DOC wifi password: ");
  Serial.println(doc[wifisettings.wm_wifi_password_identifier].as<String>());
  Serial.print("DOC old ssid user: ");
  Serial.println(doc[wifisettings.wm_old_wifi_ssid_identifier].as<String>());
  Serial.print("DOC old wifi password: ");
  Serial.println(doc[wifisettings.wm_old_wifi_password_identifier].as<String>());

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;  // Thoát khỏi hàm nếu không mở được file
  } else {
    Serial.println("Success to open config file for writing");
  }

  // Ghi dữ liệu JSON vào file
  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write to config file");
  } else {
    Serial.println("Config file saved successfully");
  }
  configFile.close();
}

void handleRoot() {
  String s = MainPage;
  webServer.send(200, "text/html", s);
}

void changeWiFiParameters() {
  String new_ssid = webServer.arg("ssid");
  String new_password = webServer.arg("password");

  Serial.print("SSID lấy từ web: ");
  Serial.println(new_ssid);
  Serial.print("Pass lấy từ web: ");
  Serial.println(new_password);

  strcpy(wifisettings.oldSSID, wifisettings.SSID);
  strcpy(wifisettings.oldPassword, wifisettings.password);
  new_ssid.toCharArray(wifisettings.SSID, sizeof(wifisettings.SSID));
  new_password.toCharArray(wifisettings.password, sizeof(wifisettings.password));

  Serial.print("SSID copy vào wifisettings: ");
  Serial.println(wifisettings.SSID);
  Serial.print("Pass copy vào wifisettings: ");
  Serial.println(wifisettings.password);
  Serial.print("SSID copy vào old_SSID: ");
  Serial.println(wifisettings.oldSSID);
  Serial.print("Pass copy vào old_password: ");
  Serial.println(wifisettings.oldPassword);
  // Đặt cờ lưu cấu hình và lưu nếu cần
  saveConfig();
  // Tạo phản hồi và gửi về client
  String response = FPSTR(wifiSuccessfulConnectionWeb);  // Load HTML từ PROGMEM
  webServer.send(200, "text/html", response);

  // Khởi động lại ESP8266 để áp dụng cài đặt WiFi mới
  delay(2000);
  ESP.restart();
}

void changeMQTTParameters() {
  String new_mqtt_hostname = webServer.arg("server");
  String new_mqtt_client_ID = webServer.arg("clientID");
  String new_mqtt_port = webServer.arg("port");
  String new_mqtt_user = webServer.arg("user");
  String new_mqtt_password = webServer.arg("pass");

  new_mqtt_client_ID.toCharArray(mqttsettings.clientId, sizeof(mqttsettings.clientId));
  new_mqtt_hostname.toCharArray(mqttsettings.hostname, sizeof(mqttsettings.hostname));
  new_mqtt_port.toCharArray(mqttsettings.port, sizeof(mqttsettings.port));
  new_mqtt_user.toCharArray(mqttsettings.user, sizeof(mqttsettings.user));
  new_mqtt_password.toCharArray(mqttsettings.password, sizeof(mqttsettings.password));

  // Đặt cờ lưu cấu hình và lưu nếu cần
  saveConfig();

  String response = FPSTR(mqttSuccessfulConnectionWeb);  // Load HTML từ PROGMEM nếu cần
  response += "<div style='display: flex; margin: 20px;'><a style='color: darkmagenta;' href='http://";
  response += WiFi.localIP().toString();
  response += "'>Go to Device Page</a></div>";
  webServer.send(200, "text/html", response);
  delay(2000);

  ESP.restart();
}

void connectToWiFi() {
  WiFi.begin(wifisettings.SSID, wifisettings.password);
  Serial.print("Đang kết nối với WiFi...");

  // Thời gian bắt đầu kết nối
  unsigned long startAttemptTime = millis();

  // Thử kết nối với WiFi mới trong 20 giây
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  // Nếu không kết nối được trong 20 giây, chuyển sang WiFi cũ
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nKhông kết nối được với WiFi mới, thử lại với WiFi cũ...");

    WiFi.begin(wifisettings.oldSSID, wifisettings.oldPassword);
    startAttemptTime = millis();

    // Thử kết nối với WiFi cũ
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
      delay(500);
      Serial.print(".");
    }

    // Kiểm tra kết nối lại
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nĐã kết nối với WiFi cũ thành công!");
    } else {
      Serial.println("\nKhông kết nối được với WiFi cũ.");
    }
  } else {
    Serial.println("\nKết nối với WiFi mới thành công!");
    Serial.print("Địa chỉ IP: ");
    Serial.println(WiFi.localIP());
  }
  createAccessPoint();
}

void setup() {
  Serial.begin(115200);

  readSettingsFromConfig();  // Đọc cấu hình từ file
  if (checkWiFiSaveInFile) {
    connectToWiFi();
  } else {
    initializeWiFiManager();  // Khởi tạo WiFiManager
  }
  if (shouldSaveConfig) {
    saveConfig();  // Lưu cấu hình nếu có thay đổi
  }`
  initializeMqttClient();  // Thiết lập MQTT

  // Thiết lập đường dẫn cho WebServer
  webServer.on("/", handleRoot);
  webServer.on("/savewifi", HTTP_POST, changeWiFiParameters);
  webServer.on("/savemqtt", HTTP_POST, changeMQTTParameters);

  webServer.begin();
  Serial.println("HTTP server started");
}

void loop() {
  connectToMQTTBroker();     // Kết nối tới MQTT Broker
  webServer.handleClient();  // Xử lý các yêu cầu từ client
}
