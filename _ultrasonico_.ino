#include <Wire.h>
const int echoPin = 27;
const int trigPin = 26;
const int ledPin = 32;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(echoPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  float distancia = leerdistancia();
  Serial.print("Distancia: ");
  Serial.print(distancia);
  Serial.println(" cm");

  delay(1000);

  if(distancia <= 30){
    delay(1000);
    digitalWrite(ledPin, HIGH);
  }else{
    digitalWrite(ledPin, LOW);
  }
}

float leerdistancia(){
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duracion = pulseIn(echoPin, HIGH, 30000);
  if (duracion == 0){
    return -1;
  }
  float distancia = (duracion * 0.0343) / 2;
  return distancia;
}