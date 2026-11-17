#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// --- Configuration ----
const char* ssid = "Optus_5EC69F";
const char* password = "restytayranhCw4";

String teamIds[] = {"135701", "133610", "135797", "134934"}; 
const int numTeams = 4;

TFT_eSPI tft = TFT_eSPI();

struct Game {
  String teamId; 
  String teamInfo;
  String displayDate; 
  String gameTime;    
  String sortKey;    
  String roundNum;
  String roundDesc;
  String venueLocation;
};

Game allGames[numTeams];
int sortedIndices[numTeams] = {0, 1, 2, 3};

Game fetchGameData(String teamId) {
  Game currentGame = {teamId, "No Game Found", "", "", "999999999999", "", "", ""};
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://www.thesportsdb.com/api/v1/json/123/eventsnext.php?id=" + teamId;
    
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      StaticJsonDocument<3000> doc;
      deserializeJson(doc, payload);

      if (doc["events"] && doc["events"][0]) {
        JsonObject event = doc["events"][0];
        String home = event["strHomeTeam"].as<String>();
        String away = event["strAwayTeam"].as<String>();
        String rawDate = event["dateEvent"].as<String>(); 
        String rawTime = event["strTime"].as<String>(); 
        
        currentGame.teamInfo = home + " vs " + away;
        currentGame.roundNum = event["intRound"].as<String>();
        currentGame.roundDesc = event["strDescriptionEN"].as<String>();
        
        String venue = event["strVenue"].as<String>();
        String city = event["strCity"].as<String>();
        
        int commaIdx = city.indexOf(',');
        if (commaIdx != -1) {
          city = city.substring(0, commaIdx);
        }
        currentGame.venueLocation = venue + ", " + city;

        struct tm tm_utc = {0};
        String fullIso = rawDate + " " + rawTime;
        
        sscanf(fullIso.c_str(), "%d-%d-%d %d:%d:%d", 
               &tm_utc.tm_year, &tm_utc.tm_mon, &tm_utc.tm_mday, 
               &tm_utc.tm_hour, &tm_utc.tm_min, &tm_utc.tm_sec);
        
        tm_utc.tm_year -= 1900; 
        tm_utc.tm_mon -= 1;    
        
        time_t time_utc = mktime(&tm_utc);
        time_t time_aest = time_utc + (10 * 3600); 
        struct tm *tm_local = localtime(&time_aest);

        char dateBuf[12];
        char timeBuf[15]; 
        char sortBuf[15];
        
        strftime(dateBuf, sizeof(dateBuf), "%d/%m/%y", tm_local);
        strftime(timeBuf, sizeof(timeBuf), "%H:%M %a", tm_local);
        strftime(sortBuf, sizeof(sortBuf), "%Y%m%d%H%M", tm_local);

        currentGame.displayDate = String(dateBuf);
        currentGame.gameTime = String(timeBuf);
        currentGame.sortKey = String(sortBuf);
      }
    }
    http.end();
  }
  return currentGame;
}

void updateDisplay() {
  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Upcoming Games", 10, 5, 4); 
  tft.drawFastHLine(0, 32, 320, TFT_DARKGREY);

  for (int i = 0; i < numTeams; i++) {
    sortedIndices[i] = i;
    allGames[i] = fetchGameData(teamIds[i]);
  }

  for (int i = 0; i < numTeams - 1; i++) {
    for (int j = i + 1; j < numTeams; j++) {
      if (allGames[sortedIndices[i]].sortKey > allGames[sortedIndices[j]].sortKey) {
        int temp = sortedIndices[i];
        sortedIndices[i] = sortedIndices[j];
        sortedIndices[j] = temp;
      }
    }
  }

  int activeRowIndex = 0;
  for (int i = 0; i < numTeams; i++) {
    int targetIdx = sortedIndices[i];
    if (allGames[targetIdx].sortKey == "999999999999") continue;

    int rowTopY = 38 + (activeRowIndex * 50); 
    tft.drawFastHLine(0, rowTopY + 46, 320, TFT_DARKGREY);

    tft.setTextColor(TFT_CYAN, TFT_BLACK); 
    tft.drawString(allGames[targetIdx].teamInfo, 10, rowTopY + 2, 2);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    String displayString = allGames[targetIdx].displayDate + " @ " + allGames[targetIdx].gameTime;
    tft.drawString(displayString, 10, rowTopY + 20, 4);

    activeRowIndex++;
  }
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH); // Backlight on

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  updateDisplay();
}

void loop() {
  static unsigned long lastUpdate = 0;

  // Refresh schedule every 30 minutes (1,800,000 ms)
  if (millis() - lastUpdate > 1800000 || lastUpdate == 0) {
    updateDisplay();
    lastUpdate = millis();
  }

  delay(1000);
}