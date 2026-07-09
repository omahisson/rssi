const int PINO_SAIDA = 2;
const int PINO_ENTRADA = 4;

void setup() {
  Serial.begin(115200);

  pinMode(PINO_SAIDA, OUTPUT);
  pinMode(PINO_ENTRADA, INPUT_PULLDOWN);
}

void loop() {
  digitalWrite(PINO_SAIDA, HIGH);
  delay(10);

  bool recebeuAlto = digitalRead(PINO_ENTRADA) == HIGH;

  digitalWrite(PINO_SAIDA, LOW);
  delay(10);

  bool recebeuBaixo = digitalRead(PINO_ENTRADA) == LOW;

  if (recebeuAlto && recebeuBaixo) {
    Serial.println("Jumper OK");
  } else {
    Serial.println("Jumper rompido");
  }

  delay(1000);
}