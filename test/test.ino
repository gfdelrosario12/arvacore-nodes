
#define TRIG_PIN 14  // D5 on NodeMCU
#define ECHO_PIN 12  // D6 on NodeMCU

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);
  delay(500);

  Serial.println();
  Serial.println("HC-SR04 ESP8266 Diagnostic");
  Serial.println("TRIG = D5 / GPIO14");
  Serial.println("ECHO = D6 / GPIO12");
}

void loop() {
  // Send a 10-microsecond trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Wait up to 30 ms for the echo
  unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    Serial.println("FAIL: No ECHO detected");
  } else {
    float distanceCm = duration * 0.0343f / 2.0f;

    Serial.print("Echo duration: ");
    Serial.print(duration);
    Serial.print(" us | Distance: ");
    Serial.print(distanceCm);
    Serial.println(" cm");
  }

  delay(1000);
}
