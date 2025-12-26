#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <MD5Builder.h>
#include "esp_ota_ops.h"

#define CHECK_UPDATE_FIRMWARE_TIME 20000 // 20 segundos

// Credenciales de tu red WiFi
const char *ssid = "ConTodaLaFe";
const char *password = "12345678";

// Reemplaza con la IP de tu PC donde corre el servidor Python
const char *firmwareUrl = "http://192.168.1.208:8080/firmware.bin";
const char *versionUrl = "http://192.168.1.208:8080/version.txt";
const char *md5Url = "http://192.168.1.208:8080/firmware.md5";



String currentVersion = "8.0.0";
String compilatedAt = __DATE__ " " __TIME__;

// Tiempo para validar el nuevo firmware (en milisegundos)
const unsigned long VALIDATION_TIME = 60000; // 60 segundos
unsigned long bootTime = 0;
bool firmwareValidated = false;

void performOTA();
void checkForUpdate();
String getFileMD5(WiFiClient *stream, size_t size);
void checkRollbackState();
void validateFirmware();

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== ESP32 OTA con Rollback Automatico ===");
  Serial.printf("Version actual: %s\n", currentVersion.c_str());

  bootTime = millis();

  // Verificar estado de rollback
  checkRollbackState();

  // Conectar WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("Conectando WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi conectado!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

    delay(2000);
    checkForUpdate();
  }
  else
  {
    Serial.println("\nError: No se pudo conectar a WiFi");
    Serial.println("ROLLBACK: Si este es un nuevo firmware, se revertira");
  }
}

void loop()
{
  // Validar firmware después del tiempo de validación
  if (!firmwareValidated && millis() - bootTime > VALIDATION_TIME)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      validateFirmware();
    }
    else
    {
      Serial.println("No se puede validar firmware: WiFi desconectado");
      Serial.println("El firmware se revertira en el proximo reinicio");
    }
  }

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
  if (millis() - lastCheck > CHECK_UPDATE_FIRMWARE_TIME)
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

void performOTA()
{
  Serial.println("\n=== Iniciando actualizacion OTA ===");

  // 1. Obtener MD5
  HTTPClient httpMD5;
  httpMD5.setTimeout(15000);
  httpMD5.begin(md5Url);
  int md5Code = httpMD5.GET();
  String expectedMD5 = "";

  if (md5Code == 200)
  {
    expectedMD5 = httpMD5.getString();
    expectedMD5.trim();
    expectedMD5.toLowerCase();
    Serial.printf("MD5 esperado: %s\n", expectedMD5.c_str());
  }
  else
  {
    Serial.println("Error: No se pudo obtener MD5");
    httpMD5.end();
    return;
  }
  httpMD5.end();

  // 2. Primera descarga: Validar MD5
  Serial.println("Descargando firmware para validar MD5...");
  HTTPClient httpCheck;
  httpCheck.setTimeout(30000);
  httpCheck.begin(firmwareUrl);
  int httpCode = httpCheck.GET();

  if (httpCode != 200)
  {
    Serial.printf("Error descargando firmware: %d\n", httpCode);
    httpCheck.end();
    return;
  }

  int contentLength = httpCheck.getSize();
  Serial.printf("Tamano del firmware: %d bytes\n", contentLength);

  WiFiClient *stream = httpCheck.getStreamPtr();
  MD5Builder md5;
  md5.begin();

  uint8_t buff[512];
  size_t bytesRead = 0;
  int lastPercent = 0;

  while (httpCheck.connected() && bytesRead < contentLength)
  {
    size_t available = stream->available();
    if (available)
    {
      size_t toRead = min(available, sizeof(buff));
      size_t read = stream->readBytes(buff, toRead);
      md5.add(buff, read);
      bytesRead += read;

      int percent = (bytesRead * 100) / contentLength;
      if (percent != lastPercent && percent % 10 == 0)
      {
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

  if (calculatedMD5 != expectedMD5)
  {
    Serial.println("ERROR: MD5 no coincide!");
    return;
  }

  Serial.println("MD5 validado correctamente!");

  // 3. Segunda descarga: Instalar firmware
  Serial.println("Descargando e instalando firmware...");
  HTTPClient httpUpdate;
  httpUpdate.setTimeout(30000);
  httpUpdate.begin(firmwareUrl);
  httpCode = httpUpdate.GET();

  if (httpCode == 200)
  {
    WiFiClient *client = httpUpdate.getStreamPtr();

    if (!Update.begin(contentLength))
    {
      Serial.println("No hay suficiente espacio para OTA");
      httpUpdate.end();
      return;
    }

    size_t written = Update.writeStream(*client);

    Serial.printf("\nBytes escritos: %d / %d\n", written, contentLength);

    if (written == contentLength)
    {
      Serial.println("Firmware escrito correctamente");
    }
    else
    {
      Serial.println("Error: escritura incompleta");
      httpUpdate.end();
      return;
    }

    if (Update.end())
    {
      if (Update.isFinished())
      {
        Serial.println("Actualizacion completada exitosamente");
        Serial.println("IMPORTANTE: El nuevo firmware debe validarse en 60 segundos");
        Serial.println("o se revertira automaticamente");
        Serial.println("Reiniciando en 3 segundos...");
        delay(3000);
        ESP.restart();
      }
      else
      {
        Serial.println("Update no finalizo correctamente");
      }
    }
    else
    {
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

void checkRollbackState()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;

  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
  {
    Serial.println("\n=== Estado de OTA ===");

    switch (ota_state)
    {
    case ESP_OTA_IMG_NEW:
      Serial.println("Estado: NUEVO (en periodo de validacion)");
      Serial.println("El firmware debe validarse en los proximos 60 segundos");
      Serial.println("o se revertira a la version anterior");
      break;

    case ESP_OTA_IMG_PENDING_VERIFY:
      Serial.println("Estado: PENDIENTE DE VERIFICACION");
      Serial.println("Este firmware necesita ser validado");
      break;

    case ESP_OTA_IMG_VALID:
      Serial.println("Estado: VALIDO");
      Serial.println("Este firmware ya fue validado exitosamente");
      firmwareValidated = true;
      break;

    case ESP_OTA_IMG_ABORTED:
      Serial.println("Estado: ABORTADO");
      Serial.println("Este firmware fallo la validacion anterior");
      break;

    case ESP_OTA_IMG_UNDEFINED:
      Serial.println("Estado: INDEFINIDO");
      break;
    }

    Serial.printf("Particion actual: %s\n", running->label);
  }
}

void validateFirmware()
{
  if (!firmwareValidated)
  {
    Serial.println("\n=== Validando nuevo firmware ===");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY || ota_state == ESP_OTA_IMG_NEW)
      {
        Serial.println("Marcando firmware como VALIDO...");

        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        {
          Serial.println("Firmware validado exitosamente!");
          Serial.println("La actualizacion OTA ha sido confirmada");
          firmwareValidated = true;
        }
        else
        {
          Serial.println("Error al marcar firmware como valido");
        }
      }
    }
  }
}
