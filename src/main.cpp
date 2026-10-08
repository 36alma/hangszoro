#include <Arduino.h>

// put function declarations here:
int myFunction(int, int);

void setup() {
  // put your setup code here, to run once:
  Serial.println("Hello world!");
  int result = myFunction(2, 3);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(5, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  // pt your main code here, to run repeatedly:

  Serial.println("Turning on the LED");
  digitalWrite(5, HIGH);
  delay(1000);
  Serial.println("Killing the LED");
  digitalWrite(5, LOW);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}

