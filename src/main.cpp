#define TINY_GSM_MODEM_A7670
#include <Arduino.h>
#include <esp_task_wdt.h>

#define MODEM_PWRKEY  14
#define MODEM_RX      25
#define MODEM_TX      26
#define MODEM_BAUD    115200

#define METER_BAUD    115200
#define METER_RXD2 32
#define METER_TXD2 33

#define ENABLE_DEBUG_LOG 1
#define WDT_TIMEOUT 3600

#define APN        "LECOSMSTC"
#define TCP_PORT   8071

#define BUFFER_SIZE 1024

const int baudrate = 115200;
const int rs_config = SERIAL_8N1;

byte buff[BUFFER_SIZE];

void debug_log(String s)
{
#if ENABLE_DEBUG_LOG
  Serial.println(s);
#endif
}

String sendAT(String cmd, uint32_t wait = 3000)
{
  Serial1.println(cmd);
  debug_log(">> " + cmd);

  String resp = "";
  uint32_t t = millis();

  while (millis() - t < wait)
  {
    while (Serial1.available())
      resp += char(Serial1.read());
  }

  debug_log(resp);
  return resp;
}


void modemPowerOn()
{
  pinMode(MODEM_PWRKEY, OUTPUT);

  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);

  delay(10000); // LTE boot time
}

bool waitSIM()
{
  for (int i = 0; i < 20; i++)
  {
    String r = sendAT("AT+CPIN?", 1000);
    if (r.indexOf("READY") >= 0)
      return true;

    delay(1000);
  }
  return false;
}

// bool waitNetwork()
// {
//   for (int i = 0; i < 40; i++)
//   {
//     String r = sendAT("AT+CREG?", 2000);

//     if (r.indexOf(",1") >= 0 || r.indexOf(",5") >= 0)
//       return true;

//     delay(2000);
//   }
//   return false;
// }

bool modemInit()
{
  sendAT("AT");
  sendAT("AT&W");
  sendAT("AT+CSQ");
  // sendAT("AT+CFUN=0", 2000);
  // sendAT("AT+CFUN=1", 5000);

    sendAT("AT+CREG?");

  if (!waitSIM())
  {
    debug_log("SIM NOT READY");
    return false;
  }

  // sendAT("AT+CNMP=38");   // LTE only
  // sendAT("AT+CMNB=1");    // CAT-1
  // sendAT("AT+CNMP=13");   // GSM only

  sendAT("AT+CNMP=13");   // Auto GSM/LTE



  sendAT("AT+CPSI?");

  // if (!waitNetwork())
  // {
  //   debug_log("NETWORK REGISTER FAIL");
  //   return false;
  // }
 sendAT("AT+CGDCONT=1," APN "\"");
 sendAT("AT+NETOPEN", 1000);
 sendAT("AT+IPADDR");

sendAT("AT+NETCLOSE=1");

// Open network stack
sendAT("AT+CIPMODE=1");

 sendAT("AT+NETOPEN");
// // 1. Open network
// if (sendAT("AT+NETOPEN", 10000).indexOf("OK") < 0)
// {
//     debug_log("NET OPEN FAIL");
//     return false;
// }


  String cmd = "AT+SERVERSTART=" + String(TCP_PORT) + ",0";

  if (sendAT(cmd, 10000).indexOf("OK") < 0)
    return false;

  debug_log("TCP SERVER READY");
  return true;

}
void enableTransparentMode() {
  debug_log("Enable transparent mode...");
  delay(1000);
  sendAT("ATE0");      // optional: echo off
}

void exitTransparentMode() {
  debug_log("Exiting transparent mode...");
  delay(1000);
  Serial1.write("+++");  // no newline, just the 3 characters
}

void setup()
{
  Serial.begin(baudrate, rs_config);
  Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  Serial2.begin(METER_BAUD, SERIAL_8N1, METER_RXD2, METER_TXD2);
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  delay(2000);
  modemPowerOn();

  if (!modemInit())
  {
   debug_log("MODEM INIT FAILED");
    while (1);
  }
}

int parseBytes(String resp) {
  int idx = resp.indexOf(',');
  if (idx < 0) return 0;  
  String numStr = resp.substring(idx + 1);
  numStr.trim();           
  return numStr.toInt();  
}

bool clientConnected()
{
  static bool connected = false;

  while (Serial1.available())
  {
    String line = Serial1.readStringUntil('\n');
    line.trim();
    debug_log("MODEM: " + line);

    if (line.indexOf("+CLIENT:") >= 0)
    {
      debug_log("CLIENT CONNECTED");
      connected = true;
      return true;
    }
  }
  connected = false;

  return false;
}
void loop()
{
  esp_task_wdt_reset();

  if (!clientConnected())
    return;
  
  debug_log("client found");

 
  // Forward data: GSM -> Serial2
  while (Serial1.available()) {
    Serial2.write(Serial1.read());
  }

  // Forward data: Serial2 -> GSM
  while (Serial2.available()) {
    Serial1.write(Serial2.read());
  }

  delay(10); // small delay
}
