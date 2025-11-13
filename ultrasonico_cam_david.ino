#include "esp_camera.h"
#include <WiFi.h>

// 🛜 Cambia estos datos por tu red
const char* ssid = "iPhone de David Antonio";
const char* password = "negro123";

// Pines del sensor ultrasónico
// ATENCIÓN: Los pines 12 y 13 están bien para el ultrasónico si no hay conflicto.
const int trigPin = 12;
const int echoPin = 13;
const float DISTANCIA_UMBRAL = 30.0; // En centímetros

// Variable para controlar si la cámara transmite
bool streamingActivo = false;

WiFiServer server(80);

// --- Función del Sensor Ultrasonico (Definicion agregada) ---
float leerDistancia() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duracion = pulseIn(echoPin, HIGH, 30000); // Timeout 30ms
  if (duracion == 0) return -1;
  float distancia = (duracion * 0.0343) / 2;
  return distancia;
}

// 📷 Configuración de la cámara (ESP32-WROVER)
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
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Error al iniciar la cámara");
    // No usamos 'while(true)' para no bloquear completamente el ESP32
    // En cambio, imprimimos el error y retornamos.
    return;
  }
}

void setup() {
  Serial.begin(115200);

  // Pines del sensor
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Cámara
  setupCamera();

  // Conexión WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConectado a WiFi!");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop() {
  // --- LÓGICA DE DETECCIÓN ULTRASÓNICA ---
  // Esta lógica se ejecuta *siempre* para actualizar 'streamingActivo'.
  
  static long last_print_time = 0;
  float distancia = leerDistancia();
  
  // Imprimir distancia cada 2 segundos para debug (para ver si funciona)
  if (millis() - last_print_time >= 2000) {
    if (distancia > 0) {
      Serial.printf("Distancia actual: %.2f cm\n", distancia);
    } else {
      Serial.println("Distancia actual: Fuera de rango o error (-1)");
    }
    last_print_time = millis();
  }

  // Activa el streaming si la distancia está dentro del umbral
  if (distancia > 0 && distancia < DISTANCIA_UMBRAL) {
    if (!streamingActivo) {
      streamingActivo = true;
      Serial.println(">>> Objeto detectado: Streaming ACTIVADO.");
    }
  } else {
    if (streamingActivo) {
      Serial.println("<<< Objeto fuera: Streaming DESACTIVADO.");
    }
    streamingActivo = false;
  }
  
  // --- SERVIDOR WEB ---
  WiFiClient client = server.available();
  if (!client) {
    // Si no hay cliente, esperamos un momento para que el ESP32 no se sature.
    delay(100); 
    return;
  }

  String req = client.readStringUntil('\r');
  client.flush();

  if (req.indexOf("/stream") != -1) { // Nota: Ya no se comprueba 'streamingActivo' aquí.
    if (!streamingActivo) {
        // Si alguien pide el stream pero no hay objeto, enviamos una respuesta temporal.
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Connection: close");
        client.println();
        client.println("<html><body><p>Streaming Inactivo: No hay objeto a menos de 30 cm.</p>");
        client.println("<p>Distancia: ");
        client.print(distancia > 0 ? String(distancia) : "N/A");
        client.println(" cm.</p>");
        client.println("</body></html>");
        return;
    }
    
    // Si llegamos aquí, streamingActivo es TRUE, iniciamos el stream.
    Serial.println("Cliente conectado. Iniciando stream.");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println();

    while (client.connected()) {
      // **LA CLAVE:** Re-chequeamos el estado del ultrasónico.
      // Si el objeto se va, la variable 'streamingActivo' se pondrá en false
      // en el próximo ciclo del loop() y saldremos de este 'while'.

      if (!streamingActivo) {
          Serial.println("Deteniendo stream: Objeto fuera de rango.");
          break; // Salir del bucle de streaming
      }

      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Error capturando frame. Deteniendo streaming.");
        break;
      }

      client.println("--frame");
      client.println("Content-Type: image/jpeg");
      client.println();
      client.write(fb->buf, fb->len);
      client.println();
      esp_camera_fb_return(fb);
      
      // Permitir un tiempo para que el sistema opere y para una lectura más rápida del sensor.
      delay(50); // 20 FPS max

      // Re-leemos el sensor dentro del bucle de streaming para una respuesta rápida.
      float current_distancia = leerDistancia();
      if (current_distancia <= 0 || current_distancia >= DISTANCIA_UMBRAL) {
          streamingActivo = false; // Esto romperá el bucle en la próxima iteración.
      }
    }
  } else {
    // Respuesta HTML estandar
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<html><body><h2>ESP32-WROVER Camara</h2>");
    client.println("<p><a href=\"/stream\">Iniciar Streaming</a></p>");
    client.println("</body></html>");
  }
}