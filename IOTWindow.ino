#include <DHT.h>
#include <WiFi.h>  

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

#define DHTPIN 21      // DHT11 센서를 연결한 핀 번호
#define DHTTYPE DHT11  // 센서 종류: DHT11
#define motorA 18
#define motorB 19
#define switchL 25
#define switchR 26
#define OPEN 1
#define CLOSE 2
#define ERROROC -1

DHT dht(DHTPIN, DHTTYPE);
WiFiClient client;
AsyncWebServer server(80);
const int maxClients = 50;  // 최대 클라이언트 수
int connectedClients = 0;   // 현재 연결된 클라이언트 수
Preferences pref;

// WIFI SSID / PW
String SSID = "CS";
String PW = "";

String city = "Gwangju";
String country = "KR";
String APIURL = "https://api.openweathermap.org/data/2.5/weather?q=";
String APIKey = "71c5405a16380a9e0fef7ad1a0060ccf";

unsigned long lastAPICall = 0;   // 마지막 API 호출 시간
long apiInterval = 60000;  // API 호출 간격 (60초)

float customTemp, inTemp, outTemp;
int customHumid, inHumid, outHumid;
int currentWeatherID;
const char *currentWeather;
float outWind;

void turnClockWise();
void turnCounterClockWise();
void setServer();
void getWeather();
int check();
void loadFlash();

void turnClockWise() {
  analogWrite(motorA, 0);
  analogWrite(motorB, 255);
}

void turnCounterClockWise() {
  analogWrite(motorA, 255);
  analogWrite(motorB, 0);
}
void turnStop() {
  analogWrite(motorA, 0);
  analogWrite(motorB, 0);
}
void setup() {

  pinMode(motorA, OUTPUT);
  pinMode(motorB, OUTPUT);
  pinMode(switchL, INPUT);
  pinMode(switchR, INPUT);
  Serial.begin(115200);
  WiFi.begin(SSID, PW);
  loadFlash();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to the WiFi network");
    Serial.println(WiFi.localIP());
    Serial.println();
  }

  getWeather();
  setServer();
  server.begin();  // 서버 시작
}

void loop() {
  // 주기적으로 API 요청 보내기
  unsigned long currentTime = millis();
  if (currentTime - lastAPICall >= apiInterval) {
    getWeather();
    lastAPICall = currentTime;
  }
}

void setServer() {

  // 웹 서버 핸들러 설정
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (connectedClients >= maxClients) {  // 최대 클라이언트 수 초과 시 거부
      request->send(503, "text/plain;charset=utf-8", "Server is busy. Please try again later.");
      return;
    }

    connectedClients++;
    String html = "<!DOCTYPE HTML><html><body>";
    html += "<h1>현재 상황</h1>";
    html += "<p>외부 온도 : " + String(outTemp) + "도</p>";
    html += "<p>내부 온도 : " + String(inTemp) + "도</p>";
    html += "<p>외부 습도 : " + String(outHumid) + "%</p>";
    html += "<p>내부 습도 : " + String(inHumid) + "%</p>";
    html += "<p>날씨 : " + String(currentWeather) + "</p>";
    html += "<p>풍속 : " + String(outWind) + "</p>";
    html += "<p>개폐여부 : " + (check() == OPEN? String("개방") : String("폐쇄")) + "</p>";
    html += "<h1>설정</h1>";
    html += "<form action=\"/set\" method=\"GET\">";
    html += "<label for=\"value1\">설정 온도 :</label>";
    html += "<input type=\"number\" id=\"value1\" name=\"value1\" min=\"0\" max=\"40\" value=\"" + String(customTemp) + "\">";
    html += "<br><br>";
    html += "<label for=\"value2\">설정 습도 :</label>";
    html += "<input type=\"number\" id=\"value2\" name=\"value2\" min=\"0\" max=\"100\" value=\"" + String(customHumid) + "\">";
    html += "<br><br>";
    html += "<label for=\"value3\">도시 이름 :</label>";
    html += "<input type=\"string\" id=\"value3\" name=\"value3\" value=\"" + city + "\">";
    html += "<br><br>";
    html += "<input type=\"submit\" value=\"Set\">";
    html += "</form>";
    html += "<br><br><h1>창문 제어</h1>";
    html += "<button onclick=\"window.location.href='/open'\">Open Window</button>";
    html += "<button onclick=\"window.location.href='/close'\">Close Window</button>";
    html += "<button onclick=\"window.location.href='/stop'\">Stop Window</button>";
    html += "<button onclick=\"window.location.href='/resume'\">Resume Window</button>";
    html += "<button onclick=\"window.location.href='/recall'\">Recall Window</button>";
    html += "</body></html>";
    request->send(200, "text/html;charset=utf-8", html);
    /*
        request->onDisconnect([](){
            connectedClients--;
            Serial.print("Client disconnected. Total clients: ");
            Serial.println(connectedClients);
        */
  });

  // 폼 제출 시 세 개의 값을 조정하는 핸들러
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value1")) {
      String value1Param = request->getParam("value1")->value();
      customTemp = value1Param.toInt();  // 첫 번째 값 저장
      pref.putFloat("customTemp", customTemp);
      lastAPICall = 0;
    }
    if (request->hasParam("value2")) {
      String value2Param = request->getParam("value2")->value();
      customHumid = value2Param.toInt();  // 두 번째 값 저장
      pref.putInt("customHumid", customHumid);
      lastAPICall = 0;
    }
    if (request->hasParam("value3")) {
      String value3Param = request->getParam("value3")->value();
      city = value3Param;  // 세 번째 값 저장
      pref.putString("city", city);
      lastAPICall = 0;
    }
    request->send(200, "text/html;charset=utf-8", "<p>Values set successfully! Go back to <a href=\"/\">home</a></p>");
  });
  // 창문 열기 요청 처리
  server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request) {
    openWindow();
    request->send(200, "text/plain", "Window Opened");
  });

  // 창문 닫기 요청 처리
  server.on("/close", HTTP_GET, [](AsyncWebServerRequest *request) {
    closeWindow();
    request->send(200, "text/plain", "Window Closed");
  });
  // 창문 정지 요청 처리
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
    apiInterval = 1000*60*60*6; // api요청 시간 6시간으로 늘림으로써 변경 금지 
    request->send(200, "text/plain", "Window stop");
  });
  // 창문 정지 해제 요청 처리
  server.on("/resume", HTTP_GET, [](AsyncWebServerRequest *request) {
    apiInterval = 1000*60; // 원래대로
    request->send(200, "text/plain", "Window resume");
  });
  server.on("/recall", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWeather(); // api 리콜
    request->send(200, "text/plain", "Window recall");
  });
}

bool setWeather() {

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Starting connection to server...");
    HTTPClient http;
    http.begin(APIURL + city + "," + country + "&appid=" + APIKey);  //Specify the URL
    int httpCode = http.GET();                                       //Make the request

    if (httpCode > 0) {
      Serial.println(httpCode);
      String payload = http.getString();
      Serial.println(payload);
      ///*
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);
      // JSON 파싱 오류 확인
      if (error) {
        Serial.print("JSON Deserialization failed: ");
        Serial.println(error.c_str());
        return false;
      }
      // JSON 데이터에서 값 추출
      outTemp = doc["main"]["temp"].as<float>() - 273.15;  // 켈빈 -> 섭씨 변환
      outHumid = doc["main"]["humidity"].as<int>();
      outWind = doc["wind"]["speed"].as<float>();
      currentWeatherID = doc["weather"]["ID"].as<int>();
      currentWeather = doc["weather"][0]["description"].as<const char *>();
      Serial.println(currentWeather);

      inHumid = dht.readHumidity();    // 습도 값 읽기
      delay(2000);
      inTemp = dht.readTemperature();  // 온도 값 읽기

      if (isnan(inTemp) || isnan(inHumid)) {
        Serial.println("센서 데이터를 읽을 수 없습니다!");
        return false;
      }
      conditionalResponse();
    } else {
      Serial.println("Error on HTTP request");
    }
    http.end();  // free resource
    return true;
    Serial.println("why not return;");
  } else {
    Serial.println("WiFi Disconnected");
    return false;
  }
}
void getWeather() {
  if (setWeather()) {
    Serial.print("현재 외부 기온은 : ");
    Serial.print(outTemp);
    Serial.println("도 입니다.");
    Serial.print("현재 외부 습도는 : ");
    Serial.print(outHumid);
    Serial.println("% 입니다.");
    Serial.print("현재 외부 풍속은 : ");
    Serial.print(outWind);
    Serial.println("m/s 입니다.");
    Serial.print("현재 내부 온도는 : ");
    Serial.println(inTemp);
    Serial.print("현재 내부 습도는 : ");
    Serial.println(inHumid);
    Serial.print("현재 날씨는 : ");
    Serial.println(currentWeather);
    /*
    http.begin(APIURL+APIKey); //Specify the URL
    int httpCode = http.GET();  //Make the request
    
    if (httpCode > 0) {
      Serial.println(httpCode);
      String result = http.getString();
      Serial.println(result);
      ///*
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(result);
      //result를 형변환해서 jsonBuffer타입으로, 
      //json parsing 사이트에서 변환해서 보면 json안에 json이 있는 경우가 있음
      //root 키 값안에 main이라는 키가 있음, 안에 또 담아주기
      JsonObject& root_main = root["main"];
      //root_main.printTo(Serial);  //json값을 시리얼모니터로확인만
      double temp = root_main["temp"];  //key값을 담으면 value가 담김
      temp = temp - 273.14;		//절대온도 고려
      Serial.print("현재 외부 기온은 : ");
      Serial.print(temp);
      Serial.println("도 입니다.");
      outTemp = temp;
        
      int humidity = root_main["humidity"];
      Serial.print("현재 외부 습도는 : ");
      Serial.print(humidity);
      Serial.println("% 입니다.");
      outHumid = humidity;
        
      JsonObject& root_wind = root["wind"];
      double wind_speed = root_wind["speed"];
      Serial.print("현재 외부 풍속은 : ");
      Serial.print(wind_speed);
      Serial.println("m/s 입니다.");

      inHumid = dht.readHumidity();     // 습도 값 읽기
      Serial.print("현재 내부 습도는 : ");
      Serial.println(inHumid);
      inTemp = dht.readTemperature(); // 온도 값 읽기

      if (isnan(inTemp) || isnan(inHumid)) {
          Serial.println("센서 데이터를 읽을 수 없습니다!");
          return;
      }
      conditionalResponse();
      delay(1000*60*5);
    
    }
    else {
      Serial.println("Error on HTTP request");
    }
    
    http.end(); // free resource
    */
  } else {
    Serial.println("why not...");
  }
}

void openWindow() {
  Serial.println("OPEN");
  turnClockWise();
  while (!(check() == OPEN)) {
    delay(10);
    turnClockWise();
  }
  turnStop();
}
void closeWindow() {
  Serial.println("CLOSE");
  turnCounterClockWise();
  while (!(check() == CLOSE)) {
    delay(10);
  }
  turnStop();
}

void conditionalResponse() {
  if (1) {
    if (inTemp < customTemp && inTemp < outTemp || inHumid < customHumid && inHumid < outHumid || currentWeatherID < 800) {
      closeWindow();
    }
  } else {
    if (currentWeatherID >= 800 && (outTemp < customTemp && outTemp < inTemp || outHumid < customHumid && outHumid < inHumid)) {
      openWindow();
    }
  }
}

void saveFlash() {
  pref.putString("city", city);
  pref.putFloat("customTemp", customTemp);
  pref.putInt("customHumid", customHumid);
  Serial.println("Save Complete");
}

void loadFlash() {
  city = pref.getString("city", "");
  customTemp = pref.getFloat("customTemp", 20.0);
  customHumid = pref.getInt("customHumid", 0);
  Serial.println("Load Complete");
}

int check() {
  if (digitalRead(switchL) == HIGH) {
    if (digitalRead(switchR) == HIGH) {
      return ERROROC;
    } else {
      return OPEN;
    }
  } else {
    if (digitalRead(switchR) == HIGH) {
      return CLOSE;
    } else {
      return 0;
    }
  }
}


/*
void loop() {
    
    delay(2000); // 센서가 데이터를 수집하는 데 필요한 시간 지연

    float humidity = dht.readHumidity();     // 습도 값 읽기
    float temperature = dht.readTemperature(); // 온도 값 읽기

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("센서 데이터를 읽을 수 없습니다!");
        return;
    }
                                                                                             
    Serial.print("습도: ");
    Serial.print(humidity);
    Serial.print(" %\t");
    Serial.print("온도: ");
    Serial.print(temperature);
    Serial.println(" *C");
    
}
*/
