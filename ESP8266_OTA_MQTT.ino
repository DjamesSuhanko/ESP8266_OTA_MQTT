#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

ESP8266WiFiMulti WiFiMulti;
HTTPClient http;

#include "/home/djames/senhas.h"

/* Versao do firmware. Pode receber um monte de parsing, mas
    nessa prova de conceito simplesmente mudarei o nome
*/
#define FW_VERSION "wemos-0.1"

//ligado, desligado e pino do LED. 4 corresponde ao 14 do ESP
#define ON  1
#define OFF 0
#define LED 4

//estrutura dos dados de login para nao precisar fazer smoothing no video
login userInfo;

#define ID   userInfo.WEMOS
#define USER userInfo.dobitaobyte
#define PASS userInfo.PASS_BROKER

//API de comunicacao com o ESP
extern "C" {
#include "user_interface.h"
}

/*Veja o post de timer com ESP8266:
  http://dobitaobyte.com.br/timer-com-esp8266-na-ide-do-arduino/ */
os_timer_t mTimer;

const char* mSSID  = userInfo.SSID;
const char* mPASS  = userInfo.PASS_WIFI;
const char* BROKER = "ns1.dobitaobyte.lan";

unsigned long timestamp = 0;

unsigned long tOld = 0;
unsigned long tNow = 0;
unsigned long tSum = 0;

char msg[15]  = {0};
char temp[15] = {0};
char stat[4]  = {0};

bool timeout   = false;
bool led_is_on = false;

WiFiClient wificlient;

/*Funcao de callback da classe PubSubClient. Se o client se subescreve
  para topicos, eh necessario ler o callback
*/
void callback(char* topic, byte* payload, unsigned int length);

/* Instancia do PubSubClient
    Broker: Endereco do broker
    Porta: 1883
    callback: funcao de callback
    atrelado: conexao wifi ou ethernet, conforme o caso
*/
PubSubClient client(BROKER, 1883, callback, wificlient);

//Funcao para limpar o array de char da mensagem
void clear(char *target, int lenght) {
  for (int i = 0; i < lenght; i++) {
    target[i] = 0;
  }
}

//Pega informacao de um topico
void getTopic(char *mTopic) {
  strcpy(msg, "/mcu/");
  strcat(msg, mTopic);
  client.subscribe(msg);
  Serial.print("Topic: ");
  Serial.println(msg);
  clear(msg, 15);
}

//Funcao de callback do timer. esta tudo declarado mas nao esta em uso
void tCallback(void *tCall) {
  timeout = true;
}

//inicializador do timer
void usrInit(void) {
  os_timer_setfn(&mTimer, tCallback, NULL);
  os_timer_arm(&mTimer, 1000, true);
}

//funcao para iniciar a conexao wifi
void connectWiFi() {
  WiFi.begin(mSSID, mPASS);
  Serial.print("Trying to connect Wifi... ");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println("Done.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

//checa a conexao com o broker. se necessario, reconecta
void checkBrokerConnection() {
  while (!client.connected()) {
    if (client.connect(ID, USER, PASS)) {
      Serial.println("Connection to broker had successfuly");
      timestamp   = millis();
      String t = String(timestamp);
      clear(temp, 15);
      t.toCharArray(temp, sizeof(t));

      client.publish("/mcu/BrokerConn", temp, 1);
      getTopic("LED");
      getTopic("fw_update");
      return;
    }
    Serial.println("Trying connect to Broker. Please, wait...");
    delay(3000);
  }
}

void doLED(byte turnOnOff) {
  digitalWrite(LED, turnOnOff - 48);
  //byte eh unsigned char, portanto 0 eh 48
  led_is_on = turnOnOff > 48 ? true : false;
}

void doUpdate(char *fullString) {
  t_httpUpdate_return ret = ESPhttpUpdate.update(fullString);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.println("HTTP_UPDATE_FAILED");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      break;
  }
}

//executa acao de acender o led ou buscar pelo firmware
void analyser(byte *msg, int lenght) {
  //se for firmware...
  if (msg[0] != '1' && msg[0] != '0') {
    char fw_name[40];
    clear(fw_name, 40);
    strcpy(fw_name, (char*)msg);
    if (strcmp(FW_VERSION, fw_name) == 0) {
      return;
    }
    Serial.println(fw_name);
    clear(fw_name, 40);
    strcpy(fw_name, "http://ns1.dobitaobyte.lan/");
    strcat(fw_name, (char*)msg);
    Serial.print("New version available: ");
    Serial.println(fw_name);
    Serial.println("Starting update. Nice to meet you. Bye!");
    doUpdate(fw_name);
    return;
  }
  doLED(msg[0]);
}

//callback da classe PubSubClient, explicado la no comeco do codigo
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("callback called: ");
  byte* p = (byte*)malloc(length);
  memcpy(p, payload, length);
  Serial.println(p[1]);
  analyser(p, length);
  client.publish("/mcu/callback", p, length);
  free(p);
}

void setup() {
  delay(2000);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, OFF);

  Serial.begin(115200);
  connectWiFi();
  //client.setServer(BROKER, 1883);
  //usrInit();
}

void loop() {
  client.loop();
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  checkBrokerConnection();

  tNow = system_get_time() / 1000;
  tSum = (tNow - tOld) / 1000;
  if (tSum > 10) {
    timeout = true;
    tOld = tNow;
  }

  if (timeout) {
    //getTopic("LED");
    //getTopic((char*)"LED");
    //getTopic((char*)"firmwareUpdate");
    clear(stat, 4);
    stat[0] = 'O';
    if (led_is_on) {
      stat[1] = 'n';
    }
    else {
      stat[1] = 'f';
      stat[2] = 'f';
    }

    client.publish("/mcu/LED_status", stat, 3);
    client.publish("/mcu/fw_version", FW_VERSION, 1);
    timeout = false;
  }
  yield();
}
