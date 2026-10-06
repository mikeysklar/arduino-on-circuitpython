// Blink the built-in LED and report over Serial.
// Exercises: a global object with a constructor, millis(), Serial.print of
// ints, hex and floats, and a helper defined after loop() (needs the
// auto-generated prototype, like the Arduino IDE).

class Counter {
public:
  Counter(int start) : count(start) {}
  int next() { return count++; }
private:
  int count;
};

Counter blinks(100);
unsigned long started;

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  started = millis();
  Serial.println("HelloSerial: setup done");
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(250);
  digitalWrite(LED_BUILTIN, LOW);
  delay(250);
  report(blinks.next());
}

void report(int n) {
  Serial.print("blink ");
  Serial.print(n);
  Serial.print(" 0x");
  Serial.print(n, HEX);
  Serial.print(" t=");
  Serial.print((millis() - started) / 1000.0, 3);
  Serial.println(" s");
}
