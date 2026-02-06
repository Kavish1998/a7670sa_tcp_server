#define TINY_GSM_MODEM_A7670
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <Arduino.h>


#define MODEM_RST 13
#define MODEM_PWRKEY 14
#define MODEM_RX 25
#define MODEM_TX 26
#define MODEM_BAUD 115200

#define RXD2 32
#define TXD2 33

#define TCP_PORT 8071
#define BUFFER_SIZE 1024

HardwareSerial modem(1);
byte buff[BUFFER_SIZE];

void sendAT(String cmd, int waitTime = 1000) {
  modem.println(cmd);
  delay(waitTime);

  while (modem.available())
    Serial.write(modem.read());
}
void modemPowerCycle() {
  Serial.println("Power cycling modem...");
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, LOW);
  delay(100);
  digitalWrite(MODEM_RST, HIGH);
  delay(2600);
  digitalWrite(MODEM_RST, LOW);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(MODEM_PWRKEY, LOW);
}

void modemInit() {
  sendAT("AT");
  sendAT("ATE0");
  sendAT("AT+CPIN?");
  sendAT("AT+CSQ");
  sendAT("AT+CREG?");

  sendAT("AT+NETOPEN", 5000);
  delay(5000);
  sendAT("AT+IPADDR", 2000);

  sendAT("AT+CGDCONT=1,\"IP\",\"mobitelbb\"");
  sendAT("AT+NETOPEN", 4000);
  sendAT("AT+IPADDR");

  sendAT("AT+TCPSRV=1," + String(TCP_PORT), 2000);

  Serial.println("TCP server started");
}


void setup() {
  Serial.begin(115200);
  Serial2.begin(2400, SERIAL_8N1, RXD2, TXD2);
  modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  modemPowerCycle();
  delay(4000);
  modemInit();
}

void loop() {

  while (modem.available()) {
    int len = modem.readBytes(buff, BUFFER_SIZE);
    if (len > 0)
      Serial2.write(buff, len);
  }
  int size = Serial2.available();
  if (size > 0) {
    size = min(size, BUFFER_SIZE);
    Serial2.readBytes(buff, size);

    modem.print("AT+CIPSEND=0,");
    modem.println(size);
    delay(100);
    modem.write(buff, size);
  }
}
