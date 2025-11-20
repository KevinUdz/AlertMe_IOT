#include "esp_camera.h"
#include <WiFi.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <NfcAdapter.h>

// --------------------------------------------------------
// 1. CONFIGURACIÓN DE RED
// --------------------------------------------------------
const char* ssid = "iPhone de David Antonio";
const char* password = "negro123";

// --------------------------------------------------------
// 2. CONFIGURACIÓN DE PINES (NFC Y CAMARA)
// --------------------------------------------------------
// ATENCIÓN: He unificado los pines I2C para que coincidan.
// Usaremos la configuración que marcaste como importante: SDA=27, SCL=26
#define I2C_SDA 27
#define I2C_SCL 26

// Pines del Sensor Ultrasónico
const int trigPin = 12;
const int echoPin = 13;
const float DISTANCIA_UMBRAL = 30.0; 

// --------------------------------------------------------
// 3. OBJETOS Y VARIABLES GLOBALES
// --------------------------------------------------------
WiFiServer server(80);

// Instancia NFC
PN532_I2C pn532_i2c(Wire);
NfcAdapter nfc = NfcAdapter(pn532_i2c);

// Variables de estado
bool streamingActivo = false;
String lastTagId = "None";
unsigned long lastNfcReadTime = 0; // Para leer NFC sin bloquear

// --------------------------------------------------------
// 4. FUNCIONES AUXILIARES
// --------------------------------------------------------

float leerDistancia() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duracion = pulseIn(echoPin, HIGH, 30000); 
  if (duracion == 0) return -1;
  return (duracion * 0.0343) / 2;
}

void checkNFC() {
  // Esta función lee el NFC pero SIN pausar el sistema 5 segundos
  // Solo leemos si ha pasado 1 segundo desde la última lectura para no saturar
  if (millis() - lastNfcReadTime > 1000) {
    if (nfc.tagPresent()) {
      NfcTag tag = nfc.read();
      lastTagId = tag.getUidString();
      Serial.println("\n--------------------------");
      Serial.print("¡TAG NFC DETECTADO! UID: ");
      Serial.println(lastTagId);
      Serial.println("--------------------------");
    }
    lastNfcReadTime = millis();
  }
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 4;
  config.pin_d1 = 5;
  config.pin_d2 = 18;
  config.pin_d3 = 19;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 21;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  
  // AJUSTE IMPORTANTE: Configuramos la cámara para usar los mismos pines que el NFC
  // para evitar conflictos en el bus I2C si están conectados físicamente igual.
  config.pin_sccb_sda = I2C_SDA; // 27
  config.pin_sccb_scl = I2C_SCL; // 26
  
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12; // Calidad ajustada (10-63), menor número es mejor calidad pero más lento
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Error al iniciar la cámara");
    return;
  }
}

// --------------------------------------------------------
// 5. SETUP
// --------------------------------------------------------
void setup() {
  Serial.begin(115200);
  
  // 1. Iniciar I2C primero (Importante para NFC)
  Wire.begin(I2C_SDA, I2C_SCL); // 27, 26
  
  // 2. Iniciar Ultrasonico
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // 3. Iniciar NFC
  Serial.println("Iniciando NFC...");
  nfc.begin(); // Si falla, generalmente se cuelga aquí o da error en serial
  
  // 4. Iniciar Cámara
  Serial.println("Iniciando Camara...");
  setupCamera();

  // 5. Iniciar WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.begin();
}

// --------------------------------------------------------
// 6. LOOP PRINCIPAL
// --------------------------------------------------------
void loop() {
  // A. SIEMPRE revisamos la distancia
  float distancia = leerDistancia();
  
  // Lógica de activación de streaming
  if (distancia > 0 && distancia < DISTANCIA_UMBRAL) {
    if (!streamingActivo) {
      streamingActivo = true;
      Serial.println(">>> Objeto cerca: Streaming ACTIVADO.");
    }
  } else {
    if (streamingActivo) {
      Serial.println("<<< Objeto lejos: Streaming DESACTIVADO.");
    }
    streamingActivo = false;
  }

  // B. SIEMPRE revisamos el NFC (Sin bloquear)
  checkNFC();

  // C. Manejo del Servidor Web
  WiFiClient client = server.available();
  
  if (client) {
    String req = client.readStringUntil('\r');
    client.flush();

    if (req.indexOf("/stream") != -1) {
      // -- LÓGICA DEL STREAM --
      
      if (!streamingActivo) {
         // Mensaje si no hay objeto cerca
         client.println("HTTP/1.1 200 OK");
         client.println("Content-Type: text/html");
         client.println("Connection: close");
         client.println();
         client.println("<h1>Sistema en Espera</h1>");
         client.println("<p>Acerque un objeto a menos de 30cm.</p>");
         client.print("<p>Ultimo NFC leido: " + lastTagId + "</p>");
         return;
      }

      // Iniciar transmisión de video MJPEG
      Serial.println("Cliente conectado al Stream.");
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
      client.println();

      while (client.connected()) {
        // 1. Verificar si debemos cortar el stream (sensor ultrasonico)
        if (!streamingActivo) {
           Serial.println("Cortando stream por distancia.");
           break; 
        }

        // 2. Capturar Frame
        camera_fb_t * fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("Fallo captura de camara");
          break;
        }

        // 3. Enviar Frame
        client.println("--frame");
        client.println("Content-Type: image/jpeg");
        client.println();
        client.write(fb->buf, fb->len);
        client.println();
        esp_camera_fb_return(fb);
        
        // 4. Pequeña pausa para estabilidad
        delay(50);

        // 5. IMPORTANTE: Actualizar sensores dentro del bucle de video
        //    Si no hacemos esto, nunca sabremos si el objeto se fue.
        float d = leerDistancia();
        if (d <= 0 || d >= DISTANCIA_UMBRAL) {
           streamingActivo = false;
        }
        
        // Opcional: Leer NFC mientras transmite video
        // Nota: Esto puede bajar un poco los FPS del video.
        checkNFC(); 
      }
    } else {
      // -- PÁGINA DE INICIO --
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      client.println("<html><head><meta charset='utf-8'></head><body>");
      client.println("<h2>Panel de Control ESP32</h2>");
      client.println("<h3>Estado NFC: " + lastTagId + "</h3>");
      client.println("<p>Distancia actual: " + String(distancia) + " cm</p>");
      client.println("<p><a href=\"/stream\"><button style='font-size:20px;'>VER CÁMARA</button></a></p>");
      client.println("</body></html>");
    }
  }
}