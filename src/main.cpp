
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <MD5Builder.h>

// Credenciales de tu red WiFi
const char *ssid = "ConTodaLaFe";
const char *password = "12345678";

// Reemplaza con la IP de tu PC donde corre el servidor Python
const char *firmwareUrl = "http://192.168.1.208:8080/firmware.bin";
const char *versionUrl = "http://192.168.1.208:8080/version.txt";
const char *md5Url = "http://192.168.1.208:8080/firmware.md5";

String currentVersion = "2.0.0";
String compilatedAt = __DATE__ " " __TIME__;

void performOTA();
void checkForUpdate();
String getFileMD5(WiFiClient *stream, size_t size);

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== ESP32 OTA Client ===");
  Serial.printf("Version actual: %s\n", currentVersion.c_str());

  // Conectar WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");

  while (WiFi.status() != WL_CONNECTED)
  {
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
  if (millis() - lastCheck > 60000)
  {
    lastCheck = millis();
    checkForUpdate();
  }
}

void checkForUpdate()
{
  Serial.println("\n=== Verificando actualizaciones ===");

  Serial.printf("Intentando conectar a: %s\n", versionUrl);

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(versionUrl);
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String newVersion = http.getString();
    newVersion.trim();

    Serial.printf("Version actual: %s\n", currentVersion.c_str());
    Serial.printf("Version servidor: %s\n", newVersion.c_str());

    if (newVersion != currentVersion)
    {
      Serial.println("¡Nueva version disponible!");
      performOTA();
    }
    else
    {
      Serial.println("Firmware actualizado, no hay cambios");
    }
  }
  else
  {
    Serial.printf("Error HTTP: %d\n", httpCode);
  }
  http.end();
}

void performOTA() {
  Serial.println("\n=== Iniciando actualizacion OTA ===");
  
  // 1. Obtener el MD5 esperado del servidor
  HTTPClient httpMD5;
  httpMD5.begin(md5Url);
  int md5Code = httpMD5.GET();
  String expectedMD5 = "";
  
  if (md5Code == 200) {
    expectedMD5 = httpMD5.getString();
    expectedMD5.trim();
    expectedMD5.toLowerCase();
    Serial.printf("MD5 esperado: %s\n", expectedMD5.c_str());
  } else {
    Serial.println("Error: No se pudo obtener MD5");
    httpMD5.end();
    return;
  }
  httpMD5.end();
  
  // 2. Primera descarga: Calcular MD5 en streaming
  Serial.println("Descargando firmware para validar MD5...");
  HTTPClient httpCheck;
  httpCheck.begin(firmwareUrl);
  int httpCode = httpCheck.GET();
  
  if (httpCode != 200) {
    Serial.printf("Error descargando firmware: %d\n", httpCode);
    httpCheck.end();
    return;
  }
  
  int contentLength = httpCheck.getSize();
  Serial.printf("Tamano del firmware: %d bytes\n", contentLength);
  
  WiFiClient* stream = httpCheck.getStreamPtr();
  MD5Builder md5;
  md5.begin();
  
  uint8_t buff[512];
  size_t bytesRead = 0;
  int lastPercent = 0;
  
  while (httpCheck.connected() && bytesRead < contentLength) {
    size_t available = stream->available();
    if (available) {
      size_t toRead = min(available, sizeof(buff));
      size_t read = stream->readBytes(buff, toRead);
      md5.add(buff, read);
      bytesRead += read;
      
      int percent = (bytesRead * 100) / contentLength;
      if (percent != lastPercent && percent % 10 == 0) {
        Serial.printf("Validando: %d%%\n", percent);
        lastPercent = percent;
      }
    }
    delay(1);
  }
  
  md5.calculate();
  String calculatedMD5 = md5.toString();
  httpCheck.end();
  
  Serial.printf("MD5 calculado: %s\n", calculatedMD5.c_str());
  
  // 3. Validar MD5
  if (calculatedMD5 != expectedMD5) {
    Serial.println("ERROR: MD5 no coincide!");
    Serial.println("El firmware esta corrupto o fue modificado");
    return;
  }
  
  Serial.println("MD5 validado correctamente!");
  
  // 4. Segunda descarga: Escribir el firmware
  Serial.println("Descargando e instalando firmware...");
  HTTPClient httpUpdate;
  httpUpdate.setTimeout(15000);
  httpUpdate.begin(firmwareUrl);
  httpCode = httpUpdate.GET();
  
  if (httpCode == 200) {
    WiFiClient* client = httpUpdate.getStreamPtr();
    
    if (!Update.begin(contentLength)) {
      Serial.println("No hay suficiente espacio para OTA");
      httpUpdate.end();
      return;
    }
    
    size_t written = Update.writeStream(*client);
    
    Serial.printf("\nBytes escritos: %d / %d\n", written, contentLength);
    
    if (written == contentLength) {
      Serial.println("Firmware escrito correctamente");
    } else {
      Serial.println("Error: escritura incompleta");
      httpUpdate.end();
      return;
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
  }
  httpUpdate.end();
}

String getFileMD5(WiFiClient *stream, size_t size)
{
  MD5Builder md5;
  md5.begin();

  uint8_t buff[128];
  size_t bytesRead = 0;

  while (bytesRead < size)
  {
    size_t toRead = min((size_t)128, size - bytesRead);
    size_t read = stream->readBytes(buff, toRead);
    md5.add(buff, read);
    bytesRead += read;
  }

  md5.calculate();
  return md5.toString();
}
