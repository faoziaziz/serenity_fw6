#include "Arduino.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
// #include "WiFi.h"
//  #include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include <time.h>
#include "RTClib.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
// #include <semphr.h>
//  #include "Audio.h"
#include "SD.h"
#include "FS.h"
// Provide the token generation process info.
// #include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
// #include "addons/RTDBHelper.h"

#include "WiFiMulti.h"
#include "Audio.h"
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
// #define OXIDEBUG
#define DEBUG_TANGGAL
#define MODESDCARD 0 // code dengan sdcard
// Digital I/O used
#include "SD.h"
#include "SPI.h"
#include "FS.h"
// #define VERSINGAPAK
#define DEVICE_ID "3"

#define REASSIGN_PINS

#define SD_CS 38
#define SPI_MOSI 37
#define SPI_MISO 35
#define SPI_SCK 36

#define I2S_DOUT 13 // 13
#define I2S_BCLK 12 // 12
#define I2S_LRC 11  // 11

#define I2C_SDA 17
#define I2C_SCL 18

RTC_DS3231 rtc;
MAX30105 particleSensor;
WiFiUDP ntpUDP;

const byte RATE_SIZE = 4; // Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE];    // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;
bool AVtoPOSTBPM = false;
Audio audio;
WiFiMulti wifiMulti;
/** suaao **/
// String ssid = "iPhone";
// String password = "hendri2203";
String ssid = "serenity";
String password = "12345678";
const long utcOffsetInSeconds = 25200;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 0;

// NTPClient timeClient(ntpUDP, "asia.pool.ntp.org", 7 * 3600);
NTPClient timeClient(ntpUDP);

enum StateMachine
{
  AUDIOSM,
  OXIMETERSM,
  SCHEDULSM,
  POSTSM
};
unsigned long previousMillis = 0;
unsigned long interval = 30000;
const String url = "http://example.org";
const String baseurl = "https://serenity-ceria.site/api";
const String postBPMurl = "/hardware/bpm/add_bpm";
const String getUserIdURL = "/hardware/device/get_by_id_device?id_device=";
const String getJadwalFromUserIDURL = "/hardware/jadwal/get_all?nomor_rekam_medis=";
const String apiusername = "XYZSERENITYXYZ";
const String apipassword = "XYZSERENITYXYZ";
StateMachine serenitySM;
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastTime = 0;
// Timer set to 10 minutes (600000)
// unsigned long timerDelay = 600000;
// Set timer to 5 seconds (5000)
unsigned long timerDelay = 5000;

struct dataJadwal
{
  String url;
  String waktu;
  int volume;
  int isplayed;
};

struct getJadwalVar
{
  int panjang = 0;
  dataJadwal datjad[100];
};

String userFromDevice;
float sensorReadingsArr[3];
SPIClass *hspi = NULL;
/*
  other function
*/
String httpGETRequestUserFromDevice();
String httpGETRequestJadwalFromUser(String userID, getJadwalVar *gJV);
String postDataBPMtoServer(String dataBPM, String userID, String tanggal);
struct tm timeinfo;
String responseUserID;
struct audioMessage
{
  uint8_t cmd;
  const char *txt;
  uint32_t value;
  uint32_t ret;
} audioTxMessage, audioRxMessage;

enum : uint8_t
{
  SET_VOLUME,
  GET_VOLUME,
  CONNECTTOHOST,
  CONNECTTOSD
};

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void CreateQueues()
{
  audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
  audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

void audioTask(void *parameter)
{
  CreateQueues();
  if (!audioSetQueue || !audioGetQueue)
  {
    log_e("queues are not initialized");
    while (true)
    {
      ;
    } // endless loop
  }

  struct audioMessage audioRxTaskMessage;
  struct audioMessage audioTxTaskMessage;

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(15); // 0...21

  // audio.setVolume(15); // 0...21
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  // SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  Serial.begin(115200);
  SD.begin(SD_CS);

  while (true)
  {
    if (xQueueReceive(audioSetQueue, &audioRxTaskMessage, 1) == pdPASS)
    {
      if (audioRxTaskMessage.cmd == SET_VOLUME)
      {
        audioTxTaskMessage.cmd = SET_VOLUME;
        audio.setVolume(audioRxTaskMessage.value);
        audioTxTaskMessage.ret = 1;
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == CONNECTTOHOST)
      {
        audioTxTaskMessage.cmd = CONNECTTOHOST;
        audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == CONNECTTOSD)
      {
        audioTxTaskMessage.cmd = CONNECTTOSD;
        audioTxTaskMessage.ret = audio.connecttoFS(SD, audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == GET_VOLUME)
      {
        audioTxTaskMessage.cmd = GET_VOLUME;
        audioTxTaskMessage.ret = audio.getVolume();
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else
      {
        log_i("error");
      }
    }
    audio.loop();
  }
}

void audioInit()
{
  xTaskCreatePinnedToCore(
      audioTask,             /* Function to implement the task */
      "audioplay",           /* Name of the task */
      20000,                 /* Stack size in words */
      NULL,                  /* Task input parameter */
      2 | portPRIVILEGE_BIT, /* Priority of the task */
      NULL,                  /* Task handle. */
      1                      /* Core where the task should run */
  );
}

audioMessage transmitReceive(audioMessage msg)
{
  xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
  if (xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS)
  {
    if (msg.cmd != audioRxMessage.cmd)
    {
      log_e("wrong reply from message queue");
    }
  }
  return audioRxMessage;
}

void audioSetVolume(uint8_t vol)
{
  audioTxMessage.cmd = SET_VOLUME;
  audioTxMessage.value = vol;
  audioMessage RX = transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume()
{
  audioTxMessage.cmd = GET_VOLUME;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttohost(const char *host)
{
  audioTxMessage.cmd = CONNECTTOHOST;
  audioTxMessage.txt = host;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttoSD(const char *filename)
{
  audioTxMessage.cmd = CONNECTTOSD;
  audioTxMessage.txt = filename;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}
// put function declarations here:
int myFunction(int, int);
getJadwalVar gJV;
void printLocalTime()
{

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay, 10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();
}
void setup()
{

  // put your setup code here, to run once:
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  // audio.setVolume(15); // 0...21
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  // SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  Serial.begin(115200);
  SD.begin(SD_CS);
  /* inisiasi oximetri*/
  Wire.begin(I2C_SDA, I2C_SCL);
  unsigned long currentMillis = millis();
  WiFi.begin(ssid, password);
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval))
  {
    Serial.print(millis());
    Serial.println("Reconnecting to WiFi...");
    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);

    WiFi.disconnect();
    WiFi.reconnect();
    previousMillis = currentMillis;
  }
  
  neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
  serenitySM = AUDIOSM;
  audioInit();
  // particleSensor.setup();                    // Configure sensor with default settings
  // particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
  // particleSensor.setPulseAmplitudeGreen(0);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) // Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1)
      ;
  }
  particleSensor.setup();                    // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0);
  if (!rtc.begin())
  {
    Serial.println("RTC not detected");
  }
  Serial.println("RTC detected");

  timeClient.begin();
  timeClient.setTimeOffset(7 * 3600);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  /**/
  /*
  while (!timeClient.update())
  {
    Serial.println("mencoba update tanggal");
    timeClient.forceUpdate();
    delay(1000);
  };*/

  // unsigned long epochTime = timeClient.getEpochTime() - 946684800UL;
  unsigned long unix_epoch = timeClient.getEpochTime();
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  rtc.adjust(DateTime(timeinfo.tm_year, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));

  // timeClient.end();
  DateTime now = rtc.now(); // Get initial time from RTC

  audioSetVolume(20);
   audio.connecttohost("https://github.com/faoziaziz/as-hot/raw/main/opening.mp3");
   

}

void loop()
{
  // put your main code here, to run repeatedly:
  // DateTime now = rtc.now();
  // audio.loop();
  if (true)
  {

    DateTime now = rtc.now();
    Serial.println(timeClient.getHours());
    Serial.println(timeClient.getMinutes());
    Serial.println(timeClient.getSeconds());

    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    //  char buffTanggalPost4[] = "YYYY-MM-DD hh:mm:ss";

    // Serial.println(now.toString(buffTanggalPost4));
    //  Serial.println("Audio telah selesai");
    /* eksekusi BPM*/
    // SeqOximetri();
    long irValue = particleSensor.getIR();

    if (checkForBeat(irValue) == true)
    {
      // We sensed a beat!
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20)
      {
        rates[rateSpot++] = (byte)beatsPerMinute; // Store this reading in the array
        rateSpot %= RATE_SIZE;                    // Wrap variable

        // Take average of readingsghskssss jcdfh
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }

    /*
        Serial.print("IR=");
        Serial.print(irValue);
        Serial.print(", BPM=");
        Serial.print(beatsPerMinute);
        Serial.print(", Avg BPM=");
        Serial.print(beatAvg);
    */
    if (irValue < 50000) // finger not detected
    {
      // neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);
      /* get user id */
      char buffTanggalPost1[] = "YYYY-MM-DD hh:mm:ss";
      Serial.println(now.toString(buffTanggalPost1));

      /* get jadwal tiap detik ke 30 */
      char buffTanggalPost2[] = "ss";
      char buffMenitPost[] = "mm";

      /* check wifi connect or not ? */

      unsigned long currentMillis = millis();
      if (WiFi.status() != WL_CONNECTED)
      {
        if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval))
        {
          Serial.print(millis());
          Serial.println("Reconnecting to WiFi...");
          neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);

          WiFi.disconnect();
          WiFi.reconnect();
          previousMillis = currentMillis;
        }
      }
      else
      {
        neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
        if ((String(now.toString(buffMenitPost)).toInt()) % 2 == 0)
        {
          if (String(now.toString(buffTanggalPost2)).equals("30"))
          {
            if (WiFi.status() == WL_CONNECTED)
            {
              userFromDevice = httpGETRequestUserFromDevice();
              Serial.println(userFromDevice);
              /* procedure to get schedule from the server*/
              String jadwal = httpGETRequestJadwalFromUser(userFromDevice, &gJV);
              Serial.println(jadwal);
              Serial.println("panjang : " + String(gJV.panjang));

              // update rtc

              for (int x = 0; x < gJV.panjang; x++)
              {
                Serial.println(gJV.datjad[x].url);
                Serial.println(gJV.datjad[x].waktu);
              }
            }
            else
            {
              Serial.println("Wifi gak nyambung");
            }
          }
        }
      }

      /* get commmand to wait the counter until 2 minutes */
      if (AVtoPOSTBPM == true)
      {
        /* post to server with content BPM */

        if (WiFi.status() == WL_CONNECTED)
        {

          userFromDevice = httpGETRequestUserFromDevice();
          Serial.println(userFromDevice);

          // String tanggalPost = "2024-09-25 09:58:34";
          char buffTanggalPost[] = "YYYY-MM-DD hh:mm:ss";
          String responsePost = postDataBPMtoServer(String(beatsPerMinute), userFromDevice, now.toString(buffTanggalPost1));

          Serial.println(now.toString(buffTanggalPost) + responsePost);
          Serial.println("posted : " + String(beatsPerMinute));

          if (beatsPerMinute < 60)
          {
            // audio jentike
            audio.connecttohost("https://github.com/faoziaziz/as-hot/raw/main/jentike.mp3");
            
          }
          // timeClient.begin();
          /*
          while (!timeClient.update())
          {
            Serial.println("mencoba update tanggal");
            timeClient.forceUpdate();
            delay(1000);
          };
          */

          if (!getLocalTime(&timeinfo))
          {
            Serial.println("Failed to obtain time");
            return;
          }
          rtc.adjust(DateTime(timeinfo.tm_year, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
          // unsigned long epochTime = timeClient.getEpochTime() - 946684800UL;
          //unsigned long unix_epoch = timeClient.getEpochTime();
         // rtc.adjust(DateTime(unix_epoch));
          DateTime now = rtc.now(); // Get initial time from RTC

          // timeClient.end();
          AVtoPOSTBPM = false;
          //
        }
        else
        {
          audio.connecttohost("https://github.com/faoziaziz/as-hot/raw/main/wifiincon_ngapak.mp3");
          AVtoPOSTBPM = false;
        }
      }

      if (gJV.panjang > 0)
      {
        DateTime now = rtc.now();

        char buffJAM[] = "hh:mm";
        int counting = 0;
        for (int y = 0; y < gJV.panjang; y++)
        {
          if (String(now.toString(buffJAM)).equals(gJV.datjad[y].waktu) && gJV.datjad[y].isplayed != 1)
          {

            gJV.datjad[y].isplayed = 1;
            Serial.println("Status played " + String(gJV.datjad[y].isplayed));

            /*update*/
            DateTime now = rtc.now();
            char buffTanggalPost[] = "YYYY-MM-DD hh:mm:ss";
            Serial.println(now.toString(buffTanggalPost));
            if (counting == 0)
            {
              counting = 1;
              if(gJV.datjad[y].volume>20){
                audioSetVolume(20);

              } else {
                audioSetVolume(gJV.datjad[y].volume);
              }
              
              audioConnecttohost(gJV.datjad[y].url.c_str());
            }

            // audio.setVolume(15);
          }
        }
        Serial.println("Ada jadwalnya bjir");
        // DateTime now = rtc.now();
        Serial.println(now.toString(buffJAM));
        delay(1000);
      }
      /* next pro*/
      /* procedure to get schedule from the server*/

      // neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
    }
    else
    {
      /* get value data befor post which is beatsPerminute*/
      neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);
      AVtoPOSTBPM = true;
    }

    // Serial.println();
  }
}

// put function definitions here:
int myFunction(int x, int y)
{
  return x + y;
}
String httpGETRequestUserFromDevice()
{

  JsonDocument doc;
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient http;
  String payload = "{}";
  if (client)
  {
    // client->setCACert(root_ca_cert);
    client->setInsecure();

    // Your Domain name with URL path or IP address with path
    http.begin(*client, (baseurl + getUserIdURL + DEVICE_ID).c_str());

    Serial.println(baseurl + getUserIdURL + DEVICE_ID);
    // If you need Node-RED/server authentication, insert user and password below
    http.setAuthorization(apiusername.c_str(), apipassword.c_str());

    // Send HTTP POST request
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      String temp_payload = http.getString();

      // get user id
      deserializeJson(doc, temp_payload);
      // payload= temp_payload;
      payload = doc["data"]["nomor_rekam_medis"].as<String>();
      ;
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
  }
  else
  {
    Serial.println("rer");
  }
  // Free resources
  http.end();

  return payload;
}

String postDataBPMtoServer(String dataBPM, String userID, String tanggal)
{
  JsonDocument doc;
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient http;
  String payload = "{}";
  if (client)
  {
    // client->setCACert(root_ca_cert);
    client->setInsecure();

    // Your Domain name with URL path or IP address with path
    http.begin(*client, (baseurl + postBPMurl).c_str());
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String httpRequestData = "nomor_rekam_medis=" + userID + "&nilai_bpm=" + dataBPM + "&tanggal=" + tanggal;
    // If you need Node-RED/server authentication, insert user and password below
    http.setAuthorization(apiusername.c_str(), apipassword.c_str());

    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      String temp_payload = http.getString();

      // get user id
      deserializeJson(doc, temp_payload);
      payload = temp_payload;
      // payload = doc["data"]["nomor_rekam_medis"].as<String>();;
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
  }
  else
  {
    Serial.println("rer");
  }
  // Free resources
  http.end();

  return payload;
}

String httpGETRequestJadwalFromUser(String userID, getJadwalVar *gJV)
{

  JsonDocument doc;
  // getJadwalVar gJV;
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient http;
  String payload = "{}";
  if (client)
  {
    // client->setCACert(root_ca_cert);
    client->setInsecure();

    // Your Domain name with URL path or IP address with path
    http.begin(*client, (baseurl + getJadwalFromUserIDURL + userID).c_str());

    Serial.println(baseurl + getJadwalFromUserIDURL + userID);
    // If you need Node-RED/server authentication, insert user and password below
    http.setAuthorization(apiusername.c_str(), apipassword.c_str());

    // Send HTTP POST request
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      String temp_payload = http.getString();

      // get user id
      deserializeJson(doc, temp_payload);
      payload = temp_payload;
      Serial.println(doc["data"][0].as<String>());
      Serial.println("Panjangnya");
      Serial.println(doc["data"].size());
      // payload = doc["data"]["nomor_rekam_medis"].as<String>();;
      gJV->panjang = doc["data"].size();

      for (int i = 0; i < doc["data"].size(); i++)
      {
        gJV->datjad[i].url = doc["data"][i]["url_audio"].as<String>();
        gJV->datjad[i].waktu = doc["data"][i]["waktu"].as<String>();
        gJV->datjad[i].volume= doc["data"][i]["volume"].as<String>().toInt();
        gJV->datjad[i].isplayed = 0;
      }
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
  }
  else
  {
    Serial.println("rer");
  }
  // Free resources
  http.end();

  return payload;
}
void audio_info(const char *info)
{
  Serial.print("info        ");
  Serial.println(info);
}