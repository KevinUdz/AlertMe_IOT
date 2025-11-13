#include "esp_camera.h"
#include <WiFi.h>

const char* ssid = "iPhone de David Antonio";
const char* password = "negro123";

const int trigPin = 12;
const int echoPin = 13;

WiFiServer server(80);
bool camaraActiva = false;

// 📷 Config cámara (ajusta a tu módulo si es diferente)
camera_config_t config;

void setupCameraConfig() {
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
}

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  setupCameraConfig();

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
  server.begin();
}

void loop() {
  float distancia = leerDistancia();
  Serial.print("Distancia: ");
  Serial.println(distancia);

  // Si detecta algo cerca -> encender cámara
  if (distancia > 0 && distancia <= 30 && !camaraActiva) {
    Serial.println("⚡ Movimiento detectado: iniciando cámara");
    if (esp_camera_init(&config) == ESP_OK) {
      camaraActiva = true;
    } else {
      Serial.println("Error iniciando cámara");
    }
  }

  // Si ya no hay nada cerca -> apagar cámara
  if ((distancia < 0 || distancia > 30) && camaraActiva) {
    Serial.println("Sin movimiento: apagando cámara");
    esp_camera_deinit();
    camaraActiva = false;
  }

  // Si la cámara está activa, permite streaming
  if (camaraActiva) {
    WiFiClient client = server.available();
    if (!client) return;
    String req = client.readStringUntil('\r');
    client.flush();

    if (req.indexOf("/stream") != -1) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
      client.println();

      while (client.connected() && camaraActiva) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (!fb) break;
        client.println("--frame");
        client.println("Content-Type: image/jpeg");
        client.println();
        client.write(fb->buf, fb->len);
        client.println();
        esp_camera_fb_return(fb);
        delay(100);
      }
    } else {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      client.println("<html><body><h2>ESP32 Cámara</h2>");
      client.println("<p><a href=\"/stream\">Ver transmisión</a></p>");
      client.println("</body></html>");
    }
  }

  delay(500);
}

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