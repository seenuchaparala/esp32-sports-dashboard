#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h> 
#include <time.h>

// --- Configuration ---
const char* ssid = "Optus_5EC69F";
const char* password = "restytayranhCw4";

// --- CYD Touch Pins ---
#define XPT2046_CS   33
#define XPT2046_CLK  25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(XPT2046_CS, 255); 

const int maxMatches = 4; 
bool rowHasGame[maxMatches] = {false, false, false, false};
bool isShowingDetailScreen = false;

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

Game allGames[maxMatches];
int sortedIndices[maxMatches] = {0, 1, 2, 3};

// --- Secure Fetch with Logic Fallbacks ---
void fetchWorldCupData() {
  for (int i = 0; i < maxMatches; i++) {
    allGames[i] = {"", "No Match Found", "", "", "999999999999", "", "", ""};
  }

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "https://www.thesportsdb.com/api/v1/json/123/eventsnextleague.php?id=4429";
  
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(24000);
    deserializeJson(doc, payload);

    if (doc["events"]) {
      JsonArray events = doc["events"].as<JsonArray>();
      
      time_t now = time(nullptr);
      struct tm *tm_now = localtime(&now);
      
      // Target range bounds
      struct tm today_2pm = *tm_now;
      today_2pm.tm_hour = 14; today_2pm.tm_min = 0; today_2pm.tm_sec = 0;
      time_t today_2pm_epoch = mktime(&today_2pm);

      struct tm tomorrow_start = *tm_now;
      tomorrow_start.tm_mday += 1; tomorrow_start.tm_hour = 0; tomorrow_start.tm_min = 0; tomorrow_start.tm_sec = 0;
      time_t tomorrow_start_epoch = mktime(&tomorrow_start);

      struct tm tomorrow_end = tomorrow_start;
      tomorrow_end.tm_mday += 1; 
      time_t tomorrow_end_epoch = mktime(&tomorrow_end);

      int matchCount = 0;
      int fallbackCount = 0;
      Game temporaryFallbackStorage[maxMatches];

      for (JsonObject event : events) {
        String home = event["strHomeTeam"].as<String>();
        String away = event["strAwayTeam"].as<String>();
        String rawDate = event["dateEvent"].as<String>(); 
        String rawTime = event["strTime"].as<String>(); 

        if (home == "" || away == "") continue;

        // Adaptive sscanf parsing handler
        struct tm tm_utc = {0};
        int parsedItems = sscanf(rawTime.c_str(), "%d:%d:%d", &tm_utc.tm_hour, &tm_utc.tm_min, &tm_utc.tm_sec);
        if(parsedItems < 2) {
          sscanf(rawTime.c_str(), "%d:%d", &tm_utc.tm_hour, &tm_utc.tm_min);
        }
        sscanf(rawDate.c_str(), "%d-%d-%d", &tm_utc.tm_year, &tm_utc.tm_mon, &tm_utc.tm_mday);
        
        tm_utc.tm_year -= 1900; 
        tm_utc.tm_mon -= 1;    
        time_t time_utc = mktime(&tm_utc);
        time_t time_aest = time_utc + (10 * 3600); // Scale to local target timezone (AEST)

        struct tm *tm_local = localtime(&time_aest);
        char dateBuf[12]; char timeBuf[15]; char sortBuf[15];
        strftime(dateBuf, sizeof(dateBuf), "%d/%m/%y", tm_local);
        strftime(timeBuf, sizeof(timeBuf), "%H:%M %a", tm_local);
        strftime(sortBuf, sizeof(sortBuf), "%Y%m%d%H%M", tm_local);

        Game parsedGame;
        parsedGame.teamId = event["idEvent"].as<String>(); 
        parsedGame.teamInfo = home + " vs " + away;
        parsedGame.roundNum = event["intRound"].as<String>();
        parsedGame.roundDesc = event["strDescriptionEN"].as<String>();
        
        String venue = event["strVenue"].as<String>();
        String city = event["strCity"].as<String>();
        int commaIdx = city.indexOf(',');
        if (commaIdx != -1) city = city.substring(0, commaIdx);
        parsedGame.venueLocation = (venue != "" ? venue : "TBD") + ", " + (city != "" ? city : "TBD");

        parsedGame.displayDate = String(dateBuf);
        parsedGame.gameTime = String(timeBuf);
        parsedGame.sortKey = String(sortBuf);

        // Populate fallback array (the next 4 upcoming matches chronologically)
        if (fallbackCount < maxMatches && time_aest >= now) {
          temporaryFallbackStorage[fallbackCount] = parsedGame;
          fallbackCount++;
        }

        // Run your custom target timeframe logic filter
        bool displayMatch = false;
        if (time_aest >= (today_2pm_epoch - 50400) && time_aest < today_2pm_epoch) { 
          displayMatch = true; 
        }
        else if (time_aest >= tomorrow_start_epoch && time_aest < tomorrow_end_epoch) {
          displayMatch = true;
        }

        if (displayMatch && matchCount < maxMatches) {
          allGames[matchCount] = parsedGame;
          matchCount++;
        }
      }

      // Fallback Trigger: If no match met the timeframe requirements, inject the upcoming schedule array instead
      if (matchCount == 0 && fallbackCount > 0) {
        for (int i = 0; i < fallbackCount; i++) {
          allGames[i] = temporaryFallbackStorage[i];
        }
      }
    }
  }
  http.end();
}

int drawMultiLineTeam(String teamStr, int startY, bool prefixWithVS) {
  int currentY = startY;
  int strLen = teamStr.length();
  int wordStart = 0;
  bool isFirstWord = true;

  for (int i = 0; i <= strLen; i++) {
    if (i == strLen || teamStr.charAt(i) == ' ') {
      String word = teamStr.substring(wordStart, i);
      word.trim();
      
      if (word.length() > 0) {
        if (word != "Football" && word != "Club" && word != "FC") {
          if (prefixWithVS && isFirstWord) {
            tft.drawCentreString("vs " + word, 160, currentY, 4);
          } else {
            tft.drawCentreString(word, 160, currentY, 4);
          }
          currentY += 22; 
          isFirstWord = false;
        }
      }
      wordStart = i + 1;
    }
  }
  return currentY;
}

void handleRowPress(int sortedIdx, String side) {
  isShowingDetailScreen = true;
  tft.fillScreen(TFT_BLACK);
  
  int originalIdx = sortedIndices[sortedIdx];
  Game game = allGames[originalIdx];
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString("WC MATCH FOCUS", 160, 5, 4);
  tft.drawFastHLine(10, 30, 300, TFT_DARKGREY);

  int vsIndex = game.teamInfo.indexOf(" vs ");
  int currentY = 36;

  if (vsIndex != -1) {
    String homeTeam = game.teamInfo.substring(0, vsIndex);
    String awayTeam = game.teamInfo.substring(vsIndex + 4);
    
    currentY = drawMultiLineTeam(homeTeam, currentY, false);
    currentY = drawMultiLineTeam(awayTeam, currentY, true);
  } else {
    currentY = drawMultiLineTeam(game.teamInfo, currentY, false);
  }

  currentY += 2;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String roundStr = "ROUND " + game.roundNum;
  if (game.roundDesc != "" && game.roundDesc != "null") {
    roundStr += " " + game.roundDesc;
  }
  if (roundStr.length() > 26) roundStr = roundStr.substring(0, 24) + "...";
  tft.drawCentreString(roundStr, 160, currentY, 2);

  currentY += 18;
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  String venueStr = game.venueLocation;
  if (venueStr.length() > 26) venueStr = venueStr.substring(0, 24) + "...";
  tft.drawCentreString(venueStr, 160, currentY, 2);

  currentY += 18;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  String timeOnly = game.gameTime.substring(0, 5);
  String dayOnly = game.gameTime.substring(6);
  String formattedTime = timeOnly + " " + dayOnly + " (AEST)";
  tft.drawCentreString(formattedTime, 160, currentY, 4);
  
  tft.drawFastHLine(10, 215, 300, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("Tap anywhere to return", 160, 222, 2);
}

void updateDisplay() {
  isShowingDetailScreen = false;
  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("World Cup Fixtures", 10, 5, 4); 
  tft.drawFastHLine(0, 32, 320, TFT_DARKGREY);

  fetchWorldCupData();

  for(int i = 0; i < maxMatches; i++) {
    rowHasGame[i] = false;
    sortedIndices[i] = i;
  }

  for (int i = 0; i < maxMatches - 1; i++) {
    for (int j = i + 1; j < maxMatches; j++) {
      if (allGames[sortedIndices[i]].sortKey > allGames[sortedIndices[j]].sortKey) {
        int temp = sortedIndices[i];
        sortedIndices[i] = sortedIndices[j];
        sortedIndices[j] = temp;
      }
    }
  }

  int activeRowIndex = 0;
  for (int i = 0; i < maxMatches; i++) {
    int targetIdx = sortedIndices[i];
    if (allGames[targetIdx].sortKey == "999999999999" || allGames[targetIdx].teamId == "") continue;

    int rowTopY = 38 + (activeRowIndex * 50); 
    tft.drawFastHLine(0, rowTopY + 46, 320, TFT_DARKGREY);

    tft.setTextColor(TFT_CYAN, TFT_BLACK); 
    String truncatedInfo = allGames[targetIdx].teamInfo;
    if(truncatedInfo.length() > 24) truncatedInfo = truncatedInfo.substring(0, 22) + "..";
    tft.drawString(truncatedInfo, 10, rowTopY + 2, 2);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    String displayString = allGames[targetIdx].displayDate + " @ " + allGames[targetIdx].gameTime;
    tft.drawString(displayString, 10, rowTopY + 20, 4);

    rowHasGame[activeRowIndex] = true;
    activeRowIndex++;
  }
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(SPI);
  ts.setRotation(1); 

  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH); 

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Set internal clock via NTP
  configTime(10 * 3600, 0, "pool.ntp.org", "time.nist.gov"); 
  struct tm timeinfo;
  int retryCounter = 0;
  while(!getLocalTime(&timeinfo) && retryCounter < 20){
     Serial.println("Synchronizing clock...");
     delay(500);
     retryCounter++;
  }
  
  updateDisplay();
}

void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastTouchTime = 0;

  if (ts.touched()) {
    if (millis() - lastTouchTime > 300) {
      TS_Point p = ts.getPoint();
      lastTouchTime = millis();
      
      int pixelX = map(p.x, 240, 3800, 0, 320); 
      int pixelY = map(p.y, 240, 3800, 0, 240);

      if (isShowingDetailScreen) {
        updateDisplay();
      } else {
        String pressedSide = (pixelX < 160) ? "LEFT" : "RIGHT";

        if (pixelY >= 35 && pixelY < 85 && rowHasGame[0]) {
          handleRowPress(0, pressedSide);
        } 
        else if (pixelY >= 85 && pixelY < 135 && rowHasGame[1]) {
          handleRowPress(1, pressedSide);
        } 
        else if (pixelY >= 135 && pixelY < 185 && rowHasGame[2]) {
          handleRowPress(2, pressedSide);
        } 
        else if (pixelY >= 185 && pixelY <= 240 && rowHasGame[3]) {
          handleRowPress(3, pressedSide);
        }
      }
    }
  }

  if (!isShowingDetailScreen) {
    if (millis() - lastUpdate > 1800000 || lastUpdate == 0) {
      updateDisplay();
      lastUpdate = millis();
    }
  }
}