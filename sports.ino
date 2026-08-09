#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h> 
#include <time.h>

// --- Configuration ---
const char* ssid = "Optus_5EC69F";
const char* password = "restytayranhCw4";

String teamIds[] = {"135701", "133610", "135797", "134934"}; 
const int numTeams = 4;

// --- CYD Touch Pins ----
#define XPT2046_CS   33
#define XPT2046_CLK  25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(XPT2046_CS, 255); 

bool rowHasGame[numTeams] = {false, false, false, false};
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

void displayTeamNewsFeed(String headerTitle, String espnUrl) {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString(headerTitle, 160, 5, 4);
  tft.drawFastHLine(10, 30, 300, TFT_DARKGREY);

  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("WiFi Disconnected", 160, 100, 2);
    return;
  }

  HTTPClient http;
  http.begin(espnUrl);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(6000);
    deserializeJson(doc, payload);

    JsonArray articles = doc["articles"];
    int currentY = 40;
    int newsCount = 0;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    for (JsonObject article : articles) {
      if (newsCount >= 3) break; 
      String headline = article["headline"].as<String>();
      
      if (headline.length() > 38) {
        String line1 = headline.substring(0, 36) + "-";
        String line2 = headline.substring(36);
        if (line2.length() > 38) line2 = line2.substring(0, 35) + "...";
        
        tft.drawString("> " + line1, 10, currentY, 2);
        currentY += 16;
        tft.drawString("  " + line2, 10, currentY, 2);
      } else {
        tft.drawString("> " + headline, 10, currentY, 2);
      }
      currentY += 24; 
      newsCount++;
    }
    if (newsCount == 0) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawCentreString("No recent updates found.", 160, 100, 2);
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("Failed to fetch feed.", 160, 100, 2);
  }
  http.end();
}

// --- NEW FUNCTION: Fetch & Display Cowboys NFL NFC East Standings ---
void displayCowboysStandings() {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString("NFC EAST STANDINGS", 160, 5, 4);
  tft.drawFastHLine(10, 30, 300, TFT_DARKGREY);

  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("WiFi Offline", 160, 100, 2);
    return;
  }

  HTTPClient http;
  // Live ESPN NFL Standings Endpoint API
  String url = "https://site.web.api.espn.com/apis/v2/sports/football/nfl/standings?seasontype=2&type=0&level=3";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(16000); // Larger allocation size required for multi-conference NFL JSON data trees
    deserializeJson(doc, payload);

    // Navigate JSON structure to locate NFC (Index 1) -> NFC East (Index 1)
    JsonArray divisions = doc["children"][1]["children"];
    JsonObject nfcEast = divisions[1]; 
    JsonArray teams = nfcEast["standings"]["entries"];

    // Render Table Headings
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("RK", 10, 42, 2);
    tft.drawString("TEAM", 50, 42, 2);
    tft.drawString("W-L-T", 190, 42, 2);
    tft.drawString("STRK", 260, 42, 2);
    tft.drawFastHLine(10, 60, 300, TFT_DARKGREY);

    int rowY = 68;
    for (JsonObject team : teams) {
      String name = team["team"]["shortDisplayName"].as<String>();
      String rank = team["stats"][20]["displayValue"].as<String>(); // Division rank entry index
      String wlt  = team["stats"][0]["displayValue"].as<String>();  // Win-Loss-Tie string
      String strk = team["stats"][12]["displayValue"].as<String>(); // Current streak string

      if(name == "Cowboys") tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Highlight Cowboys row
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);

      tft.drawString(rank, 10, rowY, 2);
      tft.drawString(name, 50, rowY, 2);
      tft.drawString(wlt, 190, rowY, 2);
      tft.drawString(strk, 260, rowY, 2);
      rowY += 32;
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("Fetch Failed", 160, 100, 2);
  }
  http.end();
}

// --- NEW FUNCTION: Fetch & Display Chelsea Contextual EPL Standings ---
void displayChelseaStandings() {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString("EPL LADDER CONTEXT", 160, 5, 4);
  tft.drawFastHLine(10, 30, 300, TFT_DARKGREY);

  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("WiFi Offline", 160, 100, 2);
    return;
  }

  HTTPClient http;
  // Live ESPN English Premier League Standings Endpoint API
  String url = "https://site.api.espn.com/apis/v2/sports/soccer/eng.1/standings";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(24000); // High-capacity footprint allocation needed for full 20-team soccer ladders
    deserializeJson(doc, payload);

    JsonArray teams = doc["children"][0]["standings"]["entries"];
    int chelseaIdx = -1;

    // First sweep: Locate Chelsea's precise index in the array data
    for (int i = 0; i < teams.size(); i++) {
      String id = teams[i]["team"]["id"].as<String>();
      if (id == "363") { // ESPN global Chelsea Team ID
        chelseaIdx = i;
        break;
      }
    }

    if (chelseaIdx == -1) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawCentreString("Chelsea data unindexed.", 160, 100, 2);
      http.end();
      return;
    }

    // Determine viewport display bounds depending on user requested ranking scenarios
    int startIdx = 0;
    int endIdx = 3; // Index limits default to Top 4 layout (0,1,2,3)

    if (chelseaIdx >= 4) {
      // Logic for outside Top 4: Show 2 teams directly above, Chelsea, and 1 team below
      startIdx = chelseaIdx - 2;
      endIdx = chelseaIdx + 1;
      
      // Safety limit adjustments if Chelsea drops down to the bottom tier boundary
      if (endIdx >= teams.size()) {
        endIdx = teams.size() - 1;
        startIdx = endIdx - 3;
      }
    }

    // Render Table Headers
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("POS", 10, 42, 2);
    tft.drawString("CLUB", 50, 42, 2);
    tft.drawString("P", 185, 42, 2);
    tft.drawString("GD", 225, 42, 2);
    tft.drawString("PTS", 270, 42, 2);
    tft.drawFastHLine(10, 60, 300, TFT_DARKGREY);

    int rowY = 68;
    for (int i = startIdx; i <= endIdx; i++) {
      JsonObject team = teams[i];
      String name = team["team"]["shortDisplayName"].as<String>();
      String pos  = String(i + 1); // Rank position mapping index offset
      String p    = team["stats"][0]["displayValue"].as<String>(); // Games Played
      String gd   = team["stats"][8]["displayValue"].as<String>(); // Goal Difference
      String pts  = team["stats"][3]["displayValue"].as<String>(); // Total Points

      if (i == chelseaIdx) tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Highlight Chelsea Row
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);

      tft.drawString(pos, 10, rowY, 2);
      tft.drawString(name, 50, rowY, 2);
      tft.drawString(p, 185, rowY, 2);
      tft.drawString(gd, 225, rowY, 2);
      tft.drawString(pts, 270, rowY, 2);
      rowY += 32;
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("Fetch Failed", 160, 100, 2);
  }
  http.end();
}

void handleRowPress(int sortedIdx, String side) {
  isShowingDetailScreen = true;
  tft.fillScreen(TFT_BLACK);
  
  int originalIdx = sortedIndices[sortedIdx];
  Game game = allGames[originalIdx];
  
  if (side == "LEFT") {
    // --- CUSTOMIZED LEFT PRESS LOGIC FOR COWBOYS & CHELSEA ---
    if (game.teamId == "134934") {
      // Dallas Cowboys Trigger -> Renders NFL Table
      displayCowboysStandings();
    }
    else if (game.teamId == "133610") {
      // Chelsea FC Trigger -> Renders Contextual EPL Table
      displayChelseaStandings();
    }
    else {
      // Standard match information layout fallback for non-customized team profiles
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawCentreString("MATCH FOCUS", 160, 5, 4);
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
    }
  } else {
    // --- RIGHT PRESS ROUTINES ---
    if (game.teamId == "133610") { 
      String chelseaUrl = "https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/news?team=363";
      displayTeamNewsFeed("CHELSEA NEWS FEED", chelseaUrl);
    } 
    else if (game.teamId == "134934") { 
      String cowboysUrl = "https://site.api.espn.com/apis/site/v2/sports/football/nfl/news?team=6";
      displayTeamNewsFeed("COWBOYS NEWS FEED", cowboysUrl);
    } 
    else {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawCentreString("TEAM ALERT", 160, 5, 4);
      tft.drawFastHLine(10, 30, 300, TFT_DARKGREY);
      
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      String msg = game.teamInfo + " right pressed";
      tft.drawCentreString(msg, 160, 100, 2);
    }
  }
  
  tft.drawFastHLine(10, 215, 300, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("Tap anywhere to return", 160, 222, 2);
}

void updateDisplay() {
  isShowingDetailScreen = false;
  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Upcoming Games", 10, 5, 4); 
  tft.drawFastHLine(0, 32, 320, TFT_DARKGREY);

  for (int i = 0; i < numTeams; i++) {
    rowHasGame[i] = false;
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