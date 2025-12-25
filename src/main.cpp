
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// Credenciales de tu red WiFi
const char *ssid = "ConTodaLaFe";
const char *password = "12345678";

// Reemplaza con la IP de tu PC donde corre el servidor Python
const char* firmwareUrl = "http://192.168.1.208:8080/firmware.bin";
const char* versionUrl = "http://192.168.1.208:8080/version.txt";

String currentVersion = "2.0.0";
String compilatedAt = __DATE__ " " __TIME__;

void performOTA();
void checkForUpdate();


void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32 OTA Client ===");
  Serial.printf("Version actual: %s\n", currentVersion.c_str());
  
  // Conectar WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\r\n¡WiFi conectado!");
  Serial.printf("IP: %s\r\n", WiFi.localIP().toString().c_str());
  Serial.printf("RSSI: %d dBm\r\n", WiFi.RSSI());

  Serial.printf("FW_VER:%s_CMP:%s\r\n", currentVersion.c_str(), compilatedAt.c_str());
  // Verificar actualización al iniciar
  delay(2000);
  checkForUpdate();
}
void loop()
{
  // Verificar que sigue conectado
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi desconectado. Reconectando...");
    WiFi.reconnect();
  }
  else
  {
    Serial.printf("WiFi conectado. RSSI: %d dBm\r\n", WiFi.RSSI());
  }

  delay(10000); // Chequear cada 10 segundos

  // Verificar actualizaciones cada 60 segundos
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) {
    lastCheck = millis();
    checkForUpdate();
  }
}




void checkForUpdate() {
  Serial.println("\n=== Verificando actualizaciones ===");
  
  HTTPClient http;
  http.begin(versionUrl);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String newVersion = http.getString();
    newVersion.trim();
    
    Serial.printf("Version actual: %s\n", currentVersion.c_str());
    Serial.printf("Version servidor: %s\n", newVersion.c_str());
    
    if (newVersion != currentVersion) {
      Serial.println("¡Nueva version disponible!");
      performOTA();
    } else {
      Serial.println("Firmware actualizado, no hay cambios");
    }
  } else {
    Serial.printf("Error HTTP: %d\n", httpCode);
  }
  http.end();
}

void performOTA() {
  Serial.println("\n=== Iniciando actualizacion OTA ===");
  
  HTTPClient http;
  http.begin(firmwareUrl);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    int contentLength = http.getSize();
    Serial.printf("Tamano del firmware: %d bytes\n", contentLength);
    
    bool canBegin = Update.begin(contentLength);
    
    if (canBegin) {
      Serial.println("Descargando firmware...");
      
      WiFiClient* client = http.getStreamPtr();
      size_t written = Update.writeStream(*client);
      
      Serial.printf("Bytes escritos: %d / %d\n", written, contentLength);
      
      if (written == contentLength) {
        Serial.println("Firmware descargado correctamente");
      } else {
        Serial.println("Error: descarga incompleta");
      }
      
      if (Update.end()) {
        if (Update.isFinished()) {
          Serial.println("Actualizacion completada exitosamente");
          Serial.println("Reiniciando en 3 segundos...");
          delay(3000);
          ESP.restart();
        } else {
          Serial.println("Update no finalizo correctamente");
        }
      } else {
        Serial.printf("Error en Update: %d\n", Update.getError());
      }
    } else {
      Serial.println("No hay suficiente espacio para OTA");
    }
  } else {
    Serial.printf("Error descargando firmware: %d\n", httpCode);
  }
  http.end();
}
