#define TINY_GSM_MODEM_A7670
#include <Arduino.h>
#include <esp_task_wdt.h>

// ----------------- PINS / UART -----------------
#define MODEM_PWRKEY 14
#define MODEM_RX     25
#define MODEM_TX     26
#define MODEM_BAUD   115200

#define METER_RXD2   32
#define METER_TXD2   33
#define METER_BAUD   2400

// ----------------- CONFIG -----------------
#define ENABLE_DEBUG_LOG 1
#define WDT_TIMEOUT 3600

#define APN        "LECOSMSTC"
#define TCP_PORT   8071

#define BUFFER_SIZE 1024

static byte buff[BUFFER_SIZE];

static bool bridgeOn = false;
static String urcLine;  // command-mode URC line buffer

// ----------------- LOG -----------------
void debug_log(const String &s)
{
#if ENABLE_DEBUG_LOG
  Serial.println(s);
#endif
}

// ----------------- AT (COMMAND MODE ONLY) -----------------
String sendAT(const String &cmd, uint32_t wait = 3000)
{
  Serial1.println(cmd);
#if ENABLE_DEBUG_LOG
  Serial.println(">> " + cmd);
#endif

  String resp;
  uint32_t t = millis();
  while (millis() - t < wait)
  {
    while (Serial1.available())
      resp += char(Serial1.read());
    delay(2);
    esp_task_wdt_reset();
  }

#if ENABLE_DEBUG_LOG
  if (resp.length()) Serial.println(resp);
#endif
  return resp;
}

// ----------------- MODEM POWER -----------------
void modemPowerOn()
{
  pinMode(MODEM_PWRKEY, OUTPUT);

  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);

  delay(10000);
}

// ----------------- INIT -----------------
bool modemInit()
{
  sendAT("AT");
  sendAT("ATE0"); // echo off
  sendAT("AT+CSQ");
  sendAT("AT+CREG?");
  sendAT("AT+CNMP=13"); // auto
  sendAT("AT+CGDCONT=1,\"IP\",\"" APN "\"");

  String r = sendAT("AT+NETOPEN", 15000);
  if (r.indexOf("OK") < 0 && r.indexOf("NETOPEN: 0") < 0)
  {
    debug_log("NETOPEN failed");
    return false;
  }

  sendAT("AT+IPADDR", 5000);

  sendAT("AT+NETCLOSE=1");

  // Transparent TCP
  sendAT("AT+CIPMODE=1");

  sendAT("AT+NETOPEN=1");


  // Start server
  String cmd = "AT+SERVERSTART=" + String(TCP_PORT) + ",0";
  r = sendAT(cmd, 10000);
  if (r.indexOf("OK") < 0)
  {
    debug_log("SERVERSTART failed");
    return false;
  }

  debug_log("TCP SERVER READY");
  return true;
}

// Enter transparent mode (only once per connection)
void enterTransparent()
{
  debug_log("BRIDGE ON (no ATO)");
  //sendAT("ATO", 2000);
  delay(300);
}

bool isConnectURC(const String &line)
{
  if (line.indexOf("RECV FROM:") >= 0) return true;
  if (line.indexOf("+CLIENT:") >= 0) return true;
  if (line.indexOf("CONNECT") >= 0)  return true;
  return false;
}

bool isDisconnectURC(const String &line)
{
  if (line.indexOf("CLOSED") >= 0)      return true;
  if (line.indexOf("NO CARRIER") >= 0)  return true;
  if (line.indexOf("+IPCLOSE") >= 0)    return true;
  if (line.indexOf("+CIPCLOSE") >= 0)   return true;
  if (line.indexOf("CLOSE") >= 0)       return true; 
  return false;
}

int pollModemURC_CommandMode()
{
  while (Serial1.available())
  {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    urcLine += c;

    if (c == '\n')
    {
      String line = urcLine;
      urcLine = "";
      line.trim();

#if ENABLE_DEBUG_LOG
      if (line.length()) Serial.println("[URC] " + line);
#endif

      if (isConnectURC(line)) return 1;
      if (isDisconnectURC(line)) return -1;
    }
  }
  return 0;
}

bool bridgeReadFromModem_ToMeter(bool &disconnectSeen)
{
  disconnectSeen = false;

  static bool droppingLine = false;
  static int matchIdx = 0;
  static const char *HDR = "RECV FROM:";

  while (Serial1.available() > 0)
  {
    int c = Serial1.read();
    if (c < 0) break;

    if (droppingLine)
    {
      if (c == '\n')
      {
        droppingLine = false;
        matchIdx = 0;
      }
      continue;
    }

    if (c == HDR[matchIdx])
    {
      matchIdx++;
      if (HDR[matchIdx] == '\0')
      {
        droppingLine = true;
        matchIdx = 0;
      }
      continue; 
    }
    else
    {
      matchIdx = 0;
    }
    byte b = (byte)c;
    Serial2.write(&b, 1);
  }

  return true;
}
void setup()
{
  Serial.begin(115200);
  Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  Serial2.begin(METER_BAUD, SERIAL_8N1, METER_RXD2, METER_TXD2);

  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  delay(2000);
  modemPowerOn();

  if (!modemInit())
  {
    debug_log("MODEM INIT FAIL -> reboot");
    delay(1000);
    ESP.restart();
  }
}

void loop()
{
  esp_task_wdt_reset();

  // -------- WAIT FOR CLIENT --------
  if (!bridgeOn)
  {
    int ev = pollModemURC_CommandMode();
    if (ev == 1)
    {
      debug_log("CLIENT CONNECTED -> BRIDGE ON");
      enterTransparent();
      bridgeOn = true;
    }
    delay(10);
    return;
  }

  bool disconnected = false;
bridgeReadFromModem_ToMeter(disconnected);


  // Meter -> Modem (raw)
  int size;
  while ((size = Serial2.available()) > 0)
  {
    size = min(size, BUFFER_SIZE);
    Serial2.readBytes(buff, size);
    Serial1.write(buff, size);
  }

  // If disconnect detected: STOP bridge. (NO AT CLOSE here!)
  if (disconnected)
  {
    debug_log("CLIENT DISCONNECTED -> BRIDGE OFF (no AT close)");
    bridgeOn = false;
    urcLine = "";
    // do NOT send AT+CIPCLOSE here (prevents AT text leaking to TCP)
  }
}
