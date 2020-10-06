#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <AESLib.h>

#include <ArduinoJWT.h>
#include <sha256.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

// Device
#define USER "iot_3"
#define PASSWORD  "9dx3LW2sRNzipTT"
// Internet
#define ENDPOINT "https://telfiregate.com"
#define STASSID "MOVISTAR_60C6"
#define STAPSK  "MBHP92xy527hTz972vZ4"
// Server
#define APPUSER "GM8rTrrXap5rzGcxNqnLaGLlqlA7O5hU950KFSkO"
#define APPPASS "fXsOXcvCcAmr7t1sKeceHu6dmZtXxf7M9ZgHCxMzi2pm002eSAOnu5ukbuHkmlAFozF2psFVOJqauIkp62QOZhpkhhoVNuQR33CmdCgN8V14v92upFKRmBeOSiIk4c9e"

// Fingerprint for demo URL, expires on June 2, 2021, needs to be updated well before this date
const char fingerprint[] PROGMEM = "25 D4 E2 DA 26 8F 38 8F 3B 68 C3 BC E4 E7 B7 D6 88 B3 54 04";

// Access token for API
String access_token = "";
String refresh_token = "";

// JWT
String key1="zrre79gzmazeq2uudhjtxkbkq147o8vn01ic8ksgeic4hrflknhx6e8fri6bewasd";
ArduinoJWT jwt = ArduinoJWT(key1);

/////// configuracion GPIOS //////
// D0 --> GPIO 16 --> pinMode(16, OUTPUT) --> PUERTA DEL PORTAL
// D1 --> GPIO 5  --> pinMode(5, OUTPUT)  --> PUERTA DEL PISO
// D2 --> GPIO 4  --> pinMode(4, INPUT)   --> CAPTOR DE MOVIMIENTO
// D3 --> GPIO 0  --> pinMode(0, INPUT)   --> SENSOR CERRADURA
// D4 --> GPIO 2  --> pinMode(2, INPUT)   --> SENSOR DE SONIDO
// D6 --> GPIO 12 --> pinMode(12, INPUT)  --> RELÉ PORTAL
// D7 --> GPIO 13 --> pinMode(12, INPUT)  --> RELÉ CERRADURA
// A0 --> ANALOG 0 --> analogRead(A0)     --> SENSOR DE TEMPERATURA 

//pin sensor cerradura
int sensor_cerradura;
//pin sensor presencia
int sensor_presencia;
//sensor temperatura
int outputpin= A0;
///// FIN CONFIGURACION Gpio`s /////////

ESP8266WiFiMulti WiFiMulti;

// NTP Client for time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600);

// Maximum delay allowed for instructions (open doors...).
const int max_age_allowed_for_payload = 120; // 2 minutes

void setup() {
  pinMode(16, OUTPUT); //portal
  pinMode(5, OUTPUT); //piso
  digitalWrite(16, HIGH);
  digitalWrite(5, HIGH);
  pinMode(0, INPUT); //cerradura
  pinMode(4, INPUT); //presencia
  pinMode(2,INPUT); //sonido
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  Serial.println();
  Serial.println();
  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  access_token = "";
 
  
 
  WiFi.mode(WIFI_STA);
  // WiFi.disconnect();
 
  WiFiMulti.cleanAPlist();
  WiFiMulti.addAP(STASSID, STAPSK);
 

  while(!WiFiMulti.run()){
    Serial.printf(".");    
    delay(2000);
  }
 
  Serial.printf("\n Wifi is ON");
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  WiFi.printDiag(Serial);

  int maxtries = 5;
  while(access_token == "" && maxtries > 0){
    maxtries -= 1;
    get_auth();
    Serial.flush();
    delay(2000);
  }

  // Time client
  timeClient.begin();
}

void loop() {
  // wait for WiFi connection
  if ((WiFiMulti.run() == WL_CONNECTED)) {
   
    // Use WiFiClient class to create TCP connections
    WiFiClient client;
   
    // get_auth();
    Serial.println(access_token);
   
    //FUNCION SENSOR CERRADURA
    bool lock_opened = status_sensor_lock();
    //FUNCION SENSOR DE PRESENCIA
    bool movement_detected = status_sensor_captor();
   
    int result = get_payload(lock_opened, movement_detected);

    if (result == 401){
      Serial.print("[AUTH] Getting new auth\n");
      int count = 3;      
      access_token = "";
      while(access_token == "" && count > 0){
        count -= 1;
        get_auth();
        delay(2000);    
      }
    }
  }else{
    Serial.printf(".");
  }
  temperature_sensor();
  audio_sensor();
  Serial.println("Wait 5s before next round...");
  delay(300);
}

StaticJsonDocument<300> getPayloadJson(String payload){
  StaticJsonDocument<300> doc;
 
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, payload);
 
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
  }
  return doc;
}

void get_auth(){    
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);

  client->setFingerprint(fingerprint);

  HTTPClient https;
  https.setTimeout(60000);

  Serial.print("[HTTPS] begin...\n");
  String url = String("") + ENDPOINT + "/o/token/";
  Serial.print("[HTTPS] begin..." + String("") + url + "\n");
  if (https.begin(*client, String("") + ENDPOINT + "/o/token/")) {  // HTTPS
   
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");
    https.setAuthorization(APPUSER, APPPASS);
   
    Serial.print("[HTTPS] POST...\n");
    // start connection and send HTTP header
    String payload = String("")+
    "grant_type=password&"+
    "username="+USER+"&"+
    "password="+PASSWORD;
   
    int httpCode = https.POST(payload);

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTPS] POST... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = https.getString();
        Serial.println(payload);
        StaticJsonDocument<300> payload_decoded = getPayloadJson(payload);
        access_token = payload_decoded["access_token"].as<String>();
        Serial.println(access_token);
      }
    } else {
      Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
      String payload = https.getString();
      Serial.println(payload);
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
}

int get_payload(bool lock_opened, bool movement_detected){  
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);

  client->setFingerprint(fingerprint);

  HTTPClient https;
  https.setTimeout(60000);

  String lock_status = String("0");
  if(lock_opened){
    lock_status = "1";
  }
 
  String movement_status = String("0");
  if(movement_detected){
    movement_status = "1";
  }

  Serial.print("[HTTPS] begin...\n");
  if (https.begin(*client, String("") + ENDPOINT + "/payload")) {  // HTTPS
   
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");
    https.addHeader("Authorization", "Bearer " + access_token);
   
    Serial.print("[HTTPS] POST...\n");
    // start connection and send HTTP header
    String payload = String("")+
    "val=Gkt"+"&"+
    "lockstatus="+lock_status+"&"+
    "movementstatus="+movement_status;
   
    int httpCode = https.POST(payload);

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTPS] POST... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = https.getString();
        Serial.println(payload);
       
        // Removes starting ["
        payload.remove(0, 2);
       
        // Removes ending "]
        int len = payload.length();
        payload.remove(len - 2, len - 1 );

        String decoded_payload = jwt_decode(payload);

        // *** JSON PARSING ***
        StaticJsonDocument<300> payload_decoded = getPayloadJson(decoded_payload);

        // Order expiracy check
        if (check_time(payload_decoded["tmt"].as<int>())){
         
          // Open Main Door
          if (payload_decoded["nstrct"].as<int>() == 101){
            open_building_door();        
          }
         
          // Open Building Door
          if (payload_decoded["nstrct"].as<int>() == 235){
            open_main_door();    
          }
         
        }else{
          Serial.print("[ERROR] Request timed out!\n");            
        }
      }
     
      // Unauthorized -> we need to do the auth again
      if (httpCode == 401){        
        https.end();
        return 401;
      }
    } else {
      Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
      Serial.println(httpCode);
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  return 0;
}

String jwt_decode(String& jwt_encoded){
  Serial.print("\n[JWT] begin decoding...\n");
  Serial.println(jwt_encoded);
 
  String jwt_decoded = "";
 
  if (jwt.decodeJWT(jwt_encoded, jwt_decoded)){
    Serial.printf("\nSuccess decoding JWT!\n");
    Serial.println(jwt_decoded);
  }else{
    Serial.printf("\nUnable to decode JWT!\n");    
  }
  return jwt_decoded;
}

String jwt_encode(String& payload_raw){
  Serial.print("\n[JWT] begin encoding...\n");
  Serial.println(payload_raw);
 
  return jwt.encodeJWT(payload_raw);
}

bool check_time(int time_to_check){
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  int dif = epochTime - time_to_check;
  // Make it positive
  if(dif < 0){
    dif = -1 * dif;
  }
 
  Serial.print("\nEpoch Time: ");
  Serial.println(epochTime);
  Serial.print("\nDifTime: ");
  Serial.println(dif);

  if(dif <= max_age_allowed_for_payload){
    return true;    
  }
  return false;
}


// ****************** SANTI ******************
bool open_building_door(){
  // Abrete Sésamo        
  // puerta del edificio
  Serial.print("[DOOR] Open Main Door\n");  
  Serial.printf ("ID del chip ESP8266 =% 08X \ n", ESP.getChipId ());
  digitalWrite(16, LOW);
  delay(1000);
  digitalWrite(16, HIGH);
  return false;
}

bool open_main_door(){
  // Abrete Sésamo      
  // puerta vivienda
  Serial.print("[DOOR] Open Building Door\n");
  Serial.printf ("ID del chip ESP8266 =% 08X \ n", ESP.getChipId ());
  digitalWrite(5, LOW);
  delay(1000);
  digitalWrite(5, HIGH);    
  return false;
}
//SENSOR CERRADURA
bool status_sensor_lock(){
  //con 0 (LOW ó FALSE) ----> puerta cerrada
  //con 1 (HIGH ó TRUE) ----> puerta abierta
  sensor_cerradura = digitalRead(0);
  if(sensor_cerradura == LOW){
    Serial.println("Puerta cerrada");
    return false;
  }
  else{
    Serial.println("Puerta abierta");
    return true;
  }
}
//SENSOR DE PRESENCIA
bool status_sensor_captor(){
  sensor_presencia = digitalRead(4);
  if(sensor_presencia == LOW){
    Serial.println("NO HAY NADIE");
    return false;
  }
  else{
    Serial.println("HAY ALGIUEN");
    return true;
  }
}
//SENSOR TEMPERATURA
bool temperature_sensor(){
  float millivolts = analogRead(A0)* 3300/1024; //3300 is the voltage provided by NodeMCU
  float celsius = millivolts/10;
  Serial.print("in DegreeC=   ");
  Serial.println(celsius);
}
//SENSOR DE SONIDO
bool audio_sensor(){
  if(digitalRead(2) == HIGH){
    Serial.println("HAY RUIDO");
  }
  else{
    Serial.println("ho hay ruido");
  }
}
