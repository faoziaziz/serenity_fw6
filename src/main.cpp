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
#define VERSINGAPAK
#define DEVICE_ID "1"

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
String ssid = "gdg-wrk";
String password = "gdg123wrk";
NTPClient timeClient(ntpUDP, "us.pool.ntp.org", 7 * 3600);
enum StateMachine
{
  AUDIOSM,
  OXIMETERSM,
  SCHEDULSM,
  POSTSM
};
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
String responseUserID;
// put function declarations here:
int myFunction(int, int);
getJadwalVar gJV;
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
  WiFi.begin(ssid, password);
  serenitySM = AUDIOSM;
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
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime() - 946684800UL;
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  audio.setVolume(15);
  // audio.connecttohost("https://github.com/faoziaziz/as-hot/raw/main/Dewa20-200120-20Pangeran20Cinta.mp3");
#ifdef VERSINGAPAK
  audio.connecttoFS(SD, "/audios/opening_ngapak.mp3");
#else
  audio.connecttoFS(SD, "/audios/opening.mp3");
#endif
}

void loop()
{
  // put your main code here, to run repeatedly:
   DateTime now = rtc.now();
  audio.loop();
  if (!audio.isRunning())
  {
    
    DateTime now = rtc.now();
   // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  //  char buffTanggalPost4[] = "YYYY-MM-DD hh:mm:ss";
  
    //Serial.println(now.toString(buffTanggalPost4));
    // Serial.println("Audio telah selesai");
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

        // Take average of readings
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
      char buffTanggalPost2[]="ss";
      if(String(now.toString(buffTanggalPost2)).equals("30")){
        if(WiFi.status()==WL_CONNECTED){
          userFromDevice = httpGETRequestUserFromDevice();
          Serial.println(userFromDevice);
          /* procedure to get schedule from the server*/
          String jadwal = httpGETRequestJadwalFromUser(userFromDevice, &gJV);
          Serial.println(jadwal);
          Serial.println("panjang : " + String(gJV.panjang));


        } else {
          Serial.println("Wifi gak nyambung");
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


          if(beatsPerMinute<60){
            //audio jentike
            audio.connecttoFS(SD, "/audios/jentike.mp3");
          }

          userFromDevice = httpGETRequestUserFromDevice();
          Serial.println(userFromDevice);

          /* procedure to get schedule from the server*/
          String jadwal = httpGETRequestJadwalFromUser(userFromDevice, &gJV);
          Serial.println(jadwal);
          Serial.println("panjang : " + String(gJV.panjang));

          for (int x = 0; x < gJV.panjang; x++)
          {
            Serial.println(gJV.datjad[x].url);
            Serial.println(gJV.datjad[x].waktu);
          }

          AVtoPOSTBPM = false;
          //
        }
        else
        {
          audio.connecttoFS(SD, "/audios/wifiincon_ngapak.mp3");
          AVtoPOSTBPM = false;
        }
      }

      if (gJV.panjang > 0)
      {
        DateTime now = rtc.now();

        char buffJAM[] = "hh:mm";

        for (int y = 0; y < gJV.panjang; y++)
        {
          if (String(now.toString(buffJAM)).equals(gJV.datjad[y].waktu) && gJV.datjad[y].isplayed != 1)
          {
            

            gJV.datjad[y].isplayed = 1;
            Serial.println( "Status played " + String(gJV.datjad[y].isplayed ));
            
            
            /*update*/
            DateTime now = rtc.now();
            char buffTanggalPost[] = "YYYY-MM-DD hh:mm:ss";
            Serial.println(now.toString(buffTanggalPost));
            audio.connecttohost(gJV.datjad[y].url.c_str());
            //audio.setVolume(15);
          }
        }
        Serial.println("Ada jadwalnya bjir");
        //DateTime now = rtc.now();
        Serial.println(now.toString(buffJAM));
        delay(1000);
      }
      /* next pro*/
      /* procedure to get schedule from the server*/

      neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
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
