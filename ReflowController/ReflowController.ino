// ----------------------------------------------------------------------------
// Reflow Oven Controller
// (c) 2019      Patrick Knöbel
// (c) 2014 Karl Pitrich <karl@pitrich.com>
// (c) 2012-2013 Ed Simmons
// ----------------------------------------------------------------------------

//Devdefins
#define NOEDGEERRORREPORT 

//Pin Mapping
#define LCD_CS      27
#define LCD_DC      23
#define LCD_RESET   22
#define SD_CS       5
#define ENC1        35
#define ENC2        32
#define ENC_B       33

#define HEATER1     17 
#define HEATER2     16 
#define ZEROX       4

#define TEMP1_CS    18
#define TEMP2_CS    19

#define BUZZER      25

#define RGB_CLK     21
#define RGB_SDO     26

//constance
#define RECAL_ZEROX_TIME_MS 100 
#define ZEROX_TIMEOUT_MS 1000 
#define READ_TEMP_INTERVAL_MS 100 
#define READ_TEMP_AVERAGE_COUNT 10 

#define RGB_LED_BRITHNESS_1TO255  125 
#define IDLE_TEMP     50
#define MAX_PROFILES  30
#define MENUE_ITEMS_VISIBLE 5
#define MENU_ITEM_HIEGT 12

//includes
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <Ticker.h>

#include "src/Adafruit_GFX_Library/Adafruit_GFX.h"
#include "src/Adafruit-ST7735-Library/Adafruit_ST7735.h"
#include "src/PID_v1/PID_v1.h"
#include "src/PID_AutoTune_v0/PID_AutoTune_v0.h"
#include "src/Menu/Menu.h"
#include "src/ClickEncoder/ClickEncoder.h"

#include "helpers.h"
#include "sin.h"
#include "root_html.h"

//structs
// data type for the values used in the reflow profile
typedef struct profileValues_s {
  int16_t soakTemp;
  int16_t soakDuration;
  int16_t peakTemp;
  int16_t peakDuration;
  float  rampUpRate;
  float  rampDownRate;
  uint8_t checksum;
} Profile_t;

typedef union {
  uint32_t value;
  uint8_t bytes[4];
} __attribute__((packed)) MAX31855_t;

typedef struct Thermocouple {
  float temperature;
  uint8_t stat;
  uint8_t chipSelect;
};

typedef enum {
  None     = 0,
  Idle     = 1,
  Settings = 2,
  Edit     = 3,

  UIMenuEnd = 9,

  RampToSoak = 10,
  Soak,
  RampUp,
  Peak,
  CoolDown,

  Complete = 20,

  Tune = 30
} State;


typedef struct {
  const Menu::Item_t *mi;
  uint8_t pos;
  bool current;
} LastItemState_t;


//prototypes
bool menuExit(const Menu::Action_t);
bool cycleStart(const Menu::Action_t);
bool menuDummy(const Menu::Action_t);
bool editNumericalValue(const Menu::Action_t);
bool saveLoadProfile(const Menu::Action_t);
bool editNumericalValue(const Menu::Action_t);
bool manualHeating(const Menu::Action_t);
bool menuWiFi(const Menu::Action_t);
bool factoryReset(const Menu::Action_t);


//Varables
const char * ver = "4.0";

SPIClass MYSPI(HSPI); 
SPIClass RGBLED(VSPI); 
Adafruit_ST7735 tft = Adafruit_ST7735(&MYSPI,LCD_CS, LCD_DC, LCD_RESET);
ClickEncoder Encoder(ENC1, ENC2, ENC_B, 2);
hw_timer_t * encodertimer = NULL;
Thermocouple Temp_1;
Thermocouple Temp_2;
Menu::Engine myMenue;
portMUX_TYPE ZeroCrossingMutex = portMUX_INITIALIZER_UNLOCKED;
esp_timer_handle_t  SwitchPowerTimer;
Preferences PREF;

WebServer server(80);
WebServer serverAction(8080);

volatile boolean globalError=false;

volatile uint8_t zeroCrossingTimesPointer=0;
volatile uint64_t zeroCrossingTimes[32];
volatile uint16_t zeroCrossingDuration=0;
volatile uint16_t zeroCrossingPoint=0;

volatile uint8_t  powerHeater=0;

float aktSystemTemperature;
float aktSystemTemperatureRamp; //°C/s

int16_t tuningHeaterOutput=30;
int16_t tuningNoiseBand=1;
int16_t tuningOutputStep=10;
int16_t tuningLookbackSec=60;


int activeProfileId = 0;
Profile_t activeProfile; // the one and only instance

int16_t encAbsolute;

State currentState  = Idle;
uint64_t stateChangedTicks = 0;

// track menu item state to improve render preformance
LastItemState_t currentlyRenderedItems[MENUE_ITEMS_VISIBLE];

// ----------------------------------------------------------------------------------------------------------------------------------------
//       Name,            Label,              Next,               Previous,         Parent,           Child,            Callback
MenuItem(miExit,          "EXIT",             Menu::NullItem,     Menu::NullItem,   Menu::NullItem,   miCycleStart,     menuExit);
  MenuItem(miCycleStart,    "Start Cycle",      miEditProfile,      Menu::NullItem,   miExit,           Menu::NullItem,   cycleStart);
  MenuItem(miEditProfile,   "Edit Profile",     miLoadProfile,      miCycleStart,     miExit,           miRampUpRate,     menuDummy);
    MenuItem(miRampUpRate,    "Ramp up  ",        miSoakTemp,         Menu::NullItem,   miEditProfile,    Menu::NullItem,   editNumericalValue);
    MenuItem(miSoakTemp,      "Soak temp",        miSoakTime,         miRampUpRate,     miEditProfile,    Menu::NullItem,   editNumericalValue);
    MenuItem(miSoakTime,      "Soak time",        miPeakTemp,         miSoakTemp,       miEditProfile,    Menu::NullItem,   editNumericalValue);
    MenuItem(miPeakTemp,      "Peak temp",        miPeakTime,         miSoakTime,       miEditProfile,    Menu::NullItem,   editNumericalValue);
    MenuItem(miPeakTime,      "Peak time",        miRampDnRate,       miPeakTemp,       miEditProfile,    Menu::NullItem,   editNumericalValue);
    MenuItem(miRampDnRate,    "Ramp down",        Menu::NullItem,     miPeakTime,       miEditProfile,    Menu::NullItem,   editNumericalValue);
  MenuItem(miLoadProfile,   "Load Profile",     miSaveProfile,      miEditProfile,    miExit,           Menu::NullItem,   saveLoadProfile);
  MenuItem(miSaveProfile,   "Save Profile",     miPidSettings,      miLoadProfile,    miExit,           Menu::NullItem,   saveLoadProfile);
  MenuItem(miPidSettings,   "PID Settings",     miManual,           miSaveProfile,    miExit,           miAutoTune,       menuDummy);
    MenuItem(miAutoTune,      "Autotune",         miPidSettingP,      Menu::NullItem,   miPidSettings,    miHeaterOutput,   menuDummy);
      MenuItem(miHeaterOutput,  "Output   ",     miNoiseBand,        Menu::NullItem,   miAutoTune,       Menu::NullItem,   editNumericalValue);
      MenuItem(miNoiseBand,     "NoiseBand",        miOutputStep,       miHeaterOutput,   miAutoTune,       Menu::NullItem,   editNumericalValue);
      MenuItem(miOutputStep,    "Step     ",       miLookbackSec,      miNoiseBand,      miAutoTune,       Menu::NullItem,   editNumericalValue);
      MenuItem(miLookbackSec,   "Lookback ",      miCycleStartAT,     miOutputStep,     miAutoTune,       Menu::NullItem,   editNumericalValue);
      MenuItem(miCycleStartAT,  "Start Autotune",   Menu::NullItem,     miLookbackSec,    miAutoTune,       Menu::NullItem,   cycleStart);
    MenuItem(miPidSettingP,   "Heater Kp",        miPidSettingI,      miAutoTune,       miPidSettings,    Menu::NullItem,   editNumericalValue);
    MenuItem(miPidSettingI,   "Heater Ki",        miPidSettingD,      miPidSettingP,    miPidSettings,    Menu::NullItem,   editNumericalValue);
    MenuItem(miPidSettingD,   "Heater Kd",        Menu::NullItem,     miPidSettingI,    miPidSettings,    Menu::NullItem,   editNumericalValue);
  MenuItem(miManual,        "Manual Heating",   miWIFI,             miPidSettings,    miExit,           Menu::NullItem,   manualHeating);
  MenuItem(miWIFI,          "WIFI",             miFactoryReset,     miManual,         miExit,           miWIFIUseSaved,   menuWiFi);
    MenuItem(miWIFIUseSaved,  "Connect to Saved", Menu::NullItem,   Menu::NullItem,   miWIFI,           Menu::NullItem,   menuWiFi);
  MenuItem(miFactoryReset,  "Factory Reset",    Menu::NullItem,     miWIFI,           miExit,           Menu::NullItem,   factoryReset);

float heaterSetpoint;
float heaterInput;
float heaterOutput;

typedef struct {
  float Kp;
  float Ki;
  float Kd;
} PID_t;

PID_t heaterPID = { 4.00, 0.05,  2.00 };

PID PID(&heaterInput, &heaterOutput, &heaterSetpoint, heaterPID.Kp, heaterPID.Ki, heaterPID.Kd, DIRECT);

PID_ATune PIDTune(&heaterInput, &heaterOutput);


char buf[20]; // generic char buffer

bool menuUpdateRequest = true;
bool initialProcessDisplay = false;

uint64_t cycleStartTime=0;

//Funcktions

void reportError(char *text)
{
  globalError=true;

  //Turn off heaters
  digitalWrite(HEATER1,LOW);
  digitalWrite(HEATER2,LOW);
  
  Serial.print("Report Error: ");
  Serial.println(text);

  tft.setTextColor(ST7735_WHITE, ST7735_RED);
  tft.fillScreen(ST7735_RED);

  tft.setCursor(5, 10);
  
  tft.setTextSize(2);
  tft.println("!!!!ERROR!!!!");
  tft.println();
  tft.setTextSize(1);
  tft.setTextWrap(true);
  tft.println(text);
  tft.setTextWrap(false);


  tft.setCursor(10, 115);
  tft.println("Power off!");

  while(1){
    setLEDRGBBColor(0,0,0);
    delay(1000);
    setLEDRGBBColor(RGB_LED_BRITHNESS_1TO255,0,0);
    delay(1000);
  }
}


void setLEDRGBBColor(uint8_t r, uint8_t g, uint8_t b)
{
  RGBLED.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  RGBLED.transfer(b);
  RGBLED.transfer(g);
  RGBLED.transfer(r);
  RGBLED.endTransaction();

}


void readThermocouple(struct Thermocouple* input) {
  MAX31855_t sensor;

  uint8_t lcdState = digitalRead(LCD_CS);
  digitalWrite(LCD_CS, HIGH);
  MYSPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(input->chipSelect, LOW);
  delay(1);
  
  for (int8_t i = 3; i >= 0; i--) {
    sensor.bytes[i] = MYSPI.transfer(0x00);
  }
  digitalWrite(input->chipSelect, HIGH);
  MYSPI.endTransaction();
  digitalWrite(LCD_CS, lcdState);

  input->stat = sensor.bytes[0] & 0b111;

  uint16_t value = (sensor.value >> 18) & 0x3FFF; // mask off the sign bit and shit to the correct alignment for the temp data  
  input->temperature = value * 0.25;

}

void IRAM_ATTR zeroCrossingDetected()
{
  uint64_t time= esp_timer_get_time();
  if(time>(zeroCrossingTimes[zeroCrossingTimesPointer]+100)) //filter multiple triggering < 100µs
  {
    zeroCrossingTimesPointer = (zeroCrossingTimesPointer+1) & 0x1F;
    zeroCrossingTimes[zeroCrossingTimesPointer]=time;
  }
}

void IRAM_ATTR switchPower()
{
  static boolean nextisinPhased= false;
  static boolean nextIsZcross= false;
  uint32_t delta=1000000;
  //get zerro crossing Values    
  portENTER_CRITICAL(&ZeroCrossingMutex);
  uint16_t duration = zeroCrossingDuration;
  uint16_t xpoint =zeroCrossingPoint;
  portEXIT_CRITICAL(&ZeroCrossingMutex);        
  uint8_t setvalue=powerHeater;
  
  digitalWrite(HEATER1,LOW);
  digitalWrite(HEATER2,LOW);

  if(!globalError)
  {
    if(nextIsZcross==true)
    {
      if(setvalue!=0)
      {
        delta =((uint32_t)duration*(256-setvalue))/256;
        if(delta<500)
        {
          delta=500;          
        }
        if(delta>(duration-500))
        {
          delta=duration-500;          
        }
        nextisinPhased=true;        
      }
      else
      {
        delta=duration/2;
      }
      nextIsZcross=false;
    }
    else
    {
      if(nextisinPhased==true)
      {
        digitalWrite(HEATER1,HIGH);
        digitalWrite(HEATER2,HIGH);
        nextisinPhased=false;
      }
      if(duration>7000)
      {
        //synconice with Zerrocrossing
        uint64_t time= esp_timer_get_time();
        uint64_t nextcrossing= time - (time %duration);
        nextcrossing += xpoint;
        //200us befor ZCrossing to give the SSR time to turn off 
        nextcrossing-=500;
        //we are alwasy at least 500s awai from an crossing event!
        if(nextcrossing<time+200) 
        { 
          nextcrossing += duration;
        }
        //delta > 100us to allwo the interrupt to work
        if(nextcrossing-100>time)
        {
          delta=nextcrossing-time;
        }
        else
        {
          Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
          delta=100;          
        }
        nextIsZcross = true;    
      }

    }
  }
  esp_timer_start_once(SwitchPowerTimer,delta); 
}

void IRAM_ATTR encoderServices(void){
    Encoder.service();      
}


void spashscreen()
{
  // splash screen
  tft.setCursor(10, 30);
  tft.setTextSize(2);
  tft.print("Reflow");
  tft.setCursor(24, 48);
  tft.print("Controller");
  tft.setTextSize(1);
  tft.setCursor(52, 67);
  tft.print("v"); tft.print(ver);
  tft.setCursor(7, 109);
  tft.print("(c)2014 karl@pitrich.com");
  tft.setCursor(7, 119);
  tft.print("(c)2019 reflow@im-pro.at");
}  



bool keyboard(const char * name, char * buffer, uint32_t length, bool init, bool trigger)
{
  bool finisched=false;
  static uint32_t p;
  if(init)
  {
    p=0;
    buffer[0]=0;
    encAbsolute=0;
    trigger=false;
    tft.fillScreen(ST7735_WHITE);    
    tft.setTextColor(ST7735_BLACK, ST7735_WHITE);
    tft.setTextSize(1);
    tft.setCursor(2, 10);
    tft.print(name);
    tft.print(":");
  }

  static int numElements=0;
  static int lastencAbsolute=0;
  char start[] =  {'a', 'A', '0', '!', ':', '[', '{', 0 };
  char end[]   =  {'z', 'Z', '9', '/', '@', '_', '~', 2 };
  int x[]      =  {2,   2,   2,   68,  2,   50,  90,  2 };
  int y[]      =  {0,   10,  20,  20,  30,  30,  30,  40};
  int spacing[]=  {6,   6,   6,   6,   6,   6,   6,   60};
  int akt=0;
  
  
  if(encAbsolute<0) encAbsolute=0;
  if(encAbsolute>numElements) encAbsolute=numElements;
  
  for(int i=0;i<sizeof(start);i++){
    int charcount =0;
    for(char c=start[i];c<=end[i];c++)
    {
      tft.setCursor(x[i]+charcount*spacing[i], y[i]+50);
      charcount++;
      if(init || encAbsolute== akt || lastencAbsolute== akt){
        if(encAbsolute== akt){
          tft.setTextColor(ST7735_WHITE, ST7735_BLUE);          
        }
        else{
          tft.setTextColor(ST7735_BLACK, ST7735_WHITE);          
        }
        switch(c){
          case 0:
            tft.print("SPACE");
            if(trigger) 
            {
              buffer[p++]=' ';
              buffer[p]=0;
            }
            break;
          case 1:
            tft.print("DELETE");
            if(trigger) 
            {
              buffer[--p]=0;
            }
            break;
          case 2:
            tft.print("OK");
            if(trigger) 
            {
              finisched=true;
            }
            break;
          default:
            tft.print(c);
            if(trigger) 
            {
              buffer[p++]=c;
              buffer[p]=0;
            }
            break;
        } 
      } 
      akt++;      
    }
  }  
  
  numElements=akt-1;
  lastencAbsolute=encAbsolute;

  //update diaply
  tft.setCursor(2, 20);
  tft.setTextColor(ST7735_WHITE, ST7735_RED);
  tft.print(buffer);
  for(int i=0;i<length-p;i++)
  {
    tft.print(" "); 
  }
  
  tft.setTextColor(ST7735_BLACK, ST7735_WHITE);  
  
  return finisched;
}

void clearLastMenuItemRenderState() {
  tft.fillScreen(ST7735_WHITE);
  displayMenusInfos();
  for (uint8_t i = 0; i < MENUE_ITEMS_VISIBLE; i++) {
    currentlyRenderedItems[i].mi = NULL;
    currentlyRenderedItems[i].pos = 0xff;
    currentlyRenderedItems[i].current = false;
  }
  menuUpdateRequest = true;
}

bool menuExit(const Menu::Action_t a) {
  clearLastMenuItemRenderState();
  return false;
}

bool menuDummy(const Menu::Action_t a) {
  if(a!=Menu::actionLabel)
  {    
    clearLastMenuItemRenderState();
  }
  return true;
}

void printfloat(float val, uint8_t precision = 1) {
  ftoa(buf, val, precision);
  tft.print(buf);
}


const char * currentStateToString()
{
  #define casePrintState(state) case state: return #state;
  switch (currentState) {
    casePrintState(RampToSoak);
    casePrintState(Soak);
    casePrintState(RampUp);
    casePrintState(Peak);
    casePrintState(CoolDown);
    casePrintState(Complete);
    casePrintState(Tune);
    default: return "Idle";
  }
}


void getItemValuePointer(const Menu::Item_t *mi, float **d, int16_t **i) {
  if (mi == &miRampUpRate)  *d = &activeProfile.rampUpRate;
  if (mi == &miRampDnRate)  *d = &activeProfile.rampDownRate;
  if (mi == &miSoakTime)    *i = &activeProfile.soakDuration;
  if (mi == &miSoakTemp)    *i = &activeProfile.soakTemp;
  if (mi == &miPeakTime)    *i = &activeProfile.peakDuration;
  if (mi == &miPeakTemp)    *i = &activeProfile.peakTemp;
  if (mi == &miPidSettingP) *d = &heaterPID.Kp;
  if (mi == &miPidSettingI) *d = &heaterPID.Ki;
  if (mi == &miPidSettingD) *d = &heaterPID.Kd; 
  if (mi == &miHeaterOutput)*i = &tuningHeaterOutput;
  if (mi == &miNoiseBand)   *i = &tuningNoiseBand;
  if (mi == &miOutputStep)  *i = &tuningOutputStep;
  if (mi == &miLookbackSec) *i = &tuningLookbackSec;
}

bool isPidSetting(const Menu::Item_t *mi) {
  return mi == &miPidSettingP || mi == &miPidSettingI || mi == &miPidSettingD;
}

bool isRampSetting(const Menu::Item_t *mi) {
  return mi == &miRampUpRate || mi == &miRampDnRate;
}

bool getItemValueLabel(const Menu::Item_t *mi, char *label) {
  int16_t *iValue = NULL;
  float  *dValue = NULL;
  char *p;
  
  getItemValuePointer(mi, &dValue, &iValue);

  if (isRampSetting(mi) || isPidSetting(mi)) {
    p = label;
    ftoa(p, *dValue, (isPidSetting(mi)) ? 2 : 1); // need greater precision with pid values
    p = label;
    
    if (isRampSetting(mi)) {
      while(*p != '\0') p++;
      *p++ = 0xf7; *p++ = 'C'; *p++ = '/'; *p++ = 's';
      *p = '\0';
    }
  }
  else {
    if (mi == &miPeakTemp || mi == &miSoakTemp) {
      itostr(label, *iValue, "\367C");
    }
    if (mi == &miPeakTime || mi == &miSoakTime || mi == &miLookbackSec) {
      itostr(label, *iValue, "s");
    }
    if (mi == &miHeaterOutput || mi == &miOutputStep) {
      itostr(label, *iValue, "%");
    }
    if(mi == &miNoiseBand) {
      itostr(label, *iValue, "");
    }
  }

  return dValue || iValue;
}

bool editNumericalValue(const Menu::Action_t action) 
{ 
  bool init=false;
  if ((init=(action == Menu::actionTrigger && currentState != Edit)) || action == Menu::actionDisplay) 
  {
    if (init) 
    {
      currentState = Edit;
      tft.setTextColor(ST7735_BLACK, ST7735_WHITE);
      tft.setCursor(10, 80);
      tft.print("Edit & click to save.");
      Encoder.setAccelerationEnabled(true);
    }

    for (uint8_t i = 0; i < MENUE_ITEMS_VISIBLE; i++) 
    {
      if (currentlyRenderedItems[i].mi == myMenue.currentItem) 
      {
        uint8_t y = currentlyRenderedItems[i].pos * MENU_ITEM_HIEGT + 2;

        if (init) 
        {
          tft.fillRect(69, y - 1, 60, MENU_ITEM_HIEGT - 2, ST7735_RED);
        }

        tft.setCursor(70, y);
        break;
      }
    }

    tft.setTextColor(ST7735_WHITE, ST7735_RED);

    int16_t *iValue = NULL;
    float  *dValue = NULL;
    getItemValuePointer(myMenue.currentItem, &dValue, &iValue);

    if (isRampSetting(myMenue.currentItem) || isPidSetting(myMenue.currentItem)) 
    {
      float tmp;
      float factor = (isPidSetting(myMenue.currentItem)) ? 100 : 10;

      //no negative numbers nedded!
      if(encAbsolute<0) encAbsolute=0;       

      if (init) {
        tmp = *dValue;
        tmp *= factor;
        encAbsolute = (int16_t)tmp;
      }
      else {
        tmp = encAbsolute;
        tmp /= factor;
        *dValue = tmp;
      }      
    }
    else {
      if (init) encAbsolute = *iValue;
      else *iValue = encAbsolute;
    }

    getItemValueLabel(myMenue.currentItem, buf);
    tft.print(buf);
    tft.print(" ");
    tft.setTextColor(ST7735_BLACK, ST7735_WHITE);
  }
  else if ((action == Menu::actionParent || action == Menu::actionTrigger ) && currentState == Edit) 
  {
    currentState = Settings;
    clearLastMenuItemRenderState();

    if (isPidSetting(myMenue.currentItem)) {
      savePID();
    }

    Encoder.setAccelerationEnabled(false);
  }
  return true;
}

bool manualHeating(const Menu::Action_t action) {
  bool init=false;
  if ((init=(action == Menu::actionTrigger && currentState != Edit)) || action == Menu::actionDisplay) 
  {
    if (init) 
    {
      currentState = Edit;
      tft.setTextColor(ST7735_BLACK, ST7735_WHITE);

      encAbsolute = 0;      
      tft.setCursor(10, 100);
      tft.print("[Double]click to stop!");
    }
    if (encAbsolute > 100) encAbsolute = 100;
    if (encAbsolute <  0) encAbsolute =  0;

    tft.setCursor(10, 80);
    tft.print("Power:  ");
    tft.setTextColor(ST7735_WHITE, ST7735_RED);
    tft.print(encAbsolute);
    tft.setTextColor(ST7735_BLACK, ST7735_WHITE);    
    tft.print("%   ");
  }
  else if ((action == Menu::actionParent || action == Menu::actionTrigger ) && currentState == Edit) 
  {
    currentState = Settings;
    clearLastMenuItemRenderState();
  }
  return true;
  
}

bool menuWiFi(const Menu::Action_t action){
  static int wifiItemsCount=0;
  static Menu::Item_t *wifiItems=NULL;
  if(action==Menu::actionTrigger && myMenue.currentItem == &miWIFI)
  {
    //enter Wifi Menu:
    tft.fillScreen(ST7735_RED);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(10, 50);
    tft.print("Scanning...");
    
    //free last Menu List
    for(int i=0;i<wifiItemsCount;i++) free((void*)wifiItems[i].Label);
    if (wifiItems!=NULL) free(wifiItems);
    wifiItemsCount = WiFi.scanNetworks();
    
    //generate Menu List
    wifiItems=(Menu::Item_t *)malloc(max(1,wifiItemsCount)*sizeof(Menu::Item_t));
    if(wifiItems==NULL){
      reportError("Melloc Error!");
    }
    
    if (wifiItemsCount == 0) 
    {      
      wifiItems[0].Label="No WiFis found!";
    } 
    else 
    {
      for (int i = 0; i < wifiItemsCount; ++i) 
      {
        String name=WiFi.SSID(i);
        char * buffer= (char*)malloc(24);
        if(name==NULL){
          reportError("Melloc Error!");
        }
        name.toCharArray(buffer,24);
        if(name.length()>23){
          buffer[20]='.';
          buffer[21]='.';
          buffer[22]='.';
          buffer[23]=0;
        }
        wifiItems[i].Label=buffer;
        wifiItems[i].Callback=menuWiFi;        
      }
    }    
    //Link Meun;
    miWIFIUseSaved.Next=&wifiItems[0];
    for (int i = 0; i < max(1,wifiItemsCount); ++i) 
    {
      wifiItems[i].Next=i<(wifiItemsCount-1)?&wifiItems[i+1]:&Menu::NullItem;
      wifiItems[i].Previous=i>0?&wifiItems[i-1]:&miWIFIUseSaved;        
      wifiItems[i].Parent=&miWIFI;        
      wifiItems[i].Child=&Menu::NullItem;        
    }
    
  }
  else if(action==Menu::actionTrigger && myMenue.currentItem == &miWIFIUseSaved)    
  {
    //Conneced to Saved WiFi:
    char ssid[32],password[32];
    ssid[0]=0;
    password[0]=0;
    PREF.getString("WIFI_SSID", ssid, 32);
    PREF.getString("WIFI_PASSWORD", password, 32);

    tft.fillScreen(ST7735_RED);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(10, 50);

    if(ssid[0]==0){
      tft.print("No WiFi Saved! ");       
    }
    else
    {
      tft.print("Connecting to: ");
      tft.setCursor(10, 60);
      tft.print(ssid);
      
      WiFi.disconnect();
      
      if(password[0]==0)
      {
        WiFi.begin(ssid);            
      }
      else
      {
        WiFi.begin(ssid, password);      
      }

      tft.setCursor(10, 70);
      for(int i=0;i<20;i++){
        if (WiFi.status() == WL_CONNECTED) break;
        delay(500);
        tft.print(".");
        
      }
      tft.setCursor(10, 80);
      if (WiFi.status() == WL_CONNECTED){
        tft.print("Connected!");
      }
      else{
        tft.print("ERROR! (");      
        tft.print(WiFi.status());      
        tft.print(")");      
        tft.setCursor(10, 90);
        tft.print("Retying in the BG ...");      
      }
    }
    delay(1000);
  }
  else if(action==Menu::actionTrigger || action == Menu::actionDisplay){
    //Choose new network
    int index=-1;
    for (int i = 0; i < wifiItemsCount; i++) 
    {
      if( myMenue.currentItem == &wifiItems[i]) index=i;
    }
    if(index!=-1){
      if(WiFi.encryptionType(index) == WIFI_AUTH_OPEN){
        PREF.putString("WIFI_SSID", WiFi.SSID(index).c_str());
        PREF.putString("WIFI_PASSWORD", "");
        myMenue.navigate(&miWIFIUseSaved);
        myMenue.executeCallbackAction(Menu::actionTrigger);
      }
      else{
        //enter password
        bool init =false;
        bool trigger =false;
        static char buffer[32];
        if(action==Menu::actionTrigger && currentState != Edit)
        {
          init =true; 
          currentState = Edit;
          Encoder.setAccelerationEnabled(true);
        }
        else if(action==Menu::actionTrigger)
        {
          trigger=true;
        }
        if(keyboard("Password",buffer,32,init,trigger))
        {
          //Done!
          PREF.putString("WIFI_SSID", WiFi.SSID(index).c_str());
          PREF.putString("WIFI_PASSWORD", buffer);
          currentState = Settings;
          Encoder.setAccelerationEnabled(false);
          myMenue.navigate(&miWIFIUseSaved);
          myMenue.executeCallbackAction(Menu::actionTrigger);          
        }
        
        
      }
    }
  }
  if(action == Menu::actionParent && currentState== Edit){
    currentState = Settings;
    Encoder.setAccelerationEnabled(false);
  }
  //clear Display if needed:
  if(action!=Menu::actionLabel && currentState!= Edit)
  { 
    clearLastMenuItemRenderState();
  }
  return true;
}

bool factoryReset(const Menu::Action_t action) {
  if (action == Menu::actionTrigger && currentState != Edit) 
  {
      currentState = Edit;
      
      tft.setTextColor(ST7735_BLACK, ST7735_WHITE);
      tft.setCursor(10, 80);
      tft.print("Click to confirm");
      tft.setCursor(10, 90);
      tft.print("Doubleclick to exit");
  }
  else if ((action == Menu::actionParent || action == Menu::actionTrigger ) && currentState == Edit) 
  {
    currentState = Settings;
    if(action == Menu::actionTrigger) factoryReset();
    clearLastMenuItemRenderState();
  }
  return true;
}

bool saveLoadProfile(const Menu::Action_t action) {
  bool isLoad = myMenue.currentItem == &miLoadProfile;
  bool init=false;
  if ((init=(action == Menu::actionTrigger && currentState != Edit)) || action == Menu::actionDisplay) 
  {
    if (init) 
    {
      currentState = Edit;
      tft.setTextColor(ST7735_BLACK, ST7735_WHITE);

      encAbsolute = activeProfileId;      
      tft.setCursor(10, 90);
      tft.print("Doubleclick to exit");
    }
    if (encAbsolute > MAX_PROFILES) encAbsolute = MAX_PROFILES;
    if (encAbsolute <  0) encAbsolute =  0;

    tft.setCursor(10, 80);
    tft.print("Click to ");
    tft.print((isLoad) ? "load " : "save ");
    tft.setTextColor(ST7735_WHITE, ST7735_RED);
    tft.print(encAbsolute);
    tft.setTextColor(ST7735_BLACK, ST7735_WHITE);    
    tft.print("   ");
  }
  else if ((action == Menu::actionParent || action == Menu::actionTrigger ) && currentState == Edit) 
  {
    currentState = Settings;
    if(action == Menu::actionTrigger) (isLoad) ? loadProfile(encAbsolute) : saveProfile(encAbsolute);
    clearLastMenuItemRenderState();
  }
  return true;
}


bool cycleStart(const Menu::Action_t action) {
  if (action == Menu::actionTrigger) {

    menuExit(action);
    
    cycleStartTime= esp_timer_get_time();

    if(myMenue.currentItem == &miCycleStart)
    {
      currentState = RampToSoak;      
    }
    else
    {
      currentState = Tune;      
    }

    initialProcessDisplay = false;
    menuUpdateRequest = false;
  }

  return true;
}

void renderMenuItem(const Menu::Item_t *mi, uint8_t pos) {
  bool isCurrent = myMenue.currentItem == mi;
  uint8_t y = pos * MENU_ITEM_HIEGT + 2;

  if (currentlyRenderedItems[pos].mi == mi 
      && currentlyRenderedItems[pos].pos == pos 
      && currentlyRenderedItems[pos].current == isCurrent) 
  {
    return; // don't render the same item in the same state twice
  }

  tft.setCursor(10, y);

  // menu cursor bar
  tft.fillRect(8, y - 2, tft.width() - 16, MENU_ITEM_HIEGT, isCurrent ? ST7735_BLUE : ST7735_WHITE);
  if (isCurrent) tft.setTextColor(ST7735_WHITE, ST7735_BLUE);
  else tft.setTextColor(ST7735_BLACK, ST7735_WHITE);

  tft.print(myMenue.getLabel(mi));

  // show values if in-place editable items
  if (getItemValueLabel(mi, buf)) {
    tft.print(' '); tft.print(buf); tft.print("   ");
  }

  // mark items that have children
  if (myMenue.getChild(mi) != &Menu::NullItem) {
    tft.print(" \x10   "); // 0x10 -> filled right arrow
  }

  currentlyRenderedItems[pos].mi = mi;
  currentlyRenderedItems[pos].pos = pos;
  currentlyRenderedItems[pos].current = isCurrent;
}


void alignRightPrefix(uint16_t v) {
  if (v < 1e2) tft.print(' '); 
  if (v < 1e1) tft.print(' ');
}

void updateProcessDisplay() {
  static uint16_t starttime_s=0;
  static float pxPerS;
  static float pxPerC;
  static float estimatedTotalTime;

  const uint8_t h =  86;
  const uint8_t w = 160;
  const uint8_t yOffset =  30; // space not available for graph  
  

  uint16_t dx, dy;
  uint8_t y = 2;

  // header & initial view
  tft.setTextColor(ST7735_WHITE, ST7735_BLUE);

  if (!initialProcessDisplay) {
    initialProcessDisplay = true;

    starttime_s=esp_timer_get_time()/1000000;
    
    tft.fillScreen(ST7735_WHITE);
    tft.fillRect(0, 0, tft.width(), MENU_ITEM_HIEGT, ST7735_BLUE);
    tft.setCursor(2, y);
    
    if(currentState == Tune)
    {
      tft.print("Tuning ");
      
      estimatedTotalTime=60*10;
    
      pxPerC =  h / 300.0 ;
    }
    else{
      tft.print("Profile ");
      tft.print(activeProfileId);

      // estimate total run time for current profile
      estimatedTotalTime = activeProfile.soakDuration + activeProfile.peakDuration;
      estimatedTotalTime += (activeProfile.soakTemp - aktSystemTemperature) / (activeProfile.rampUpRate );
      estimatedTotalTime += (activeProfile.peakTemp - activeProfile.soakTemp) / (activeProfile.rampUpRate );
      estimatedTotalTime += (activeProfile.peakTemp - IDLE_TEMP) / (activeProfile.rampDownRate );
      
      pxPerC =  h / (activeProfile.peakTemp * 1.20) ;
    }
    pxPerS = 160 / estimatedTotalTime;

    // 50°C grid
    for (uint16_t tg = 0; tg < activeProfile.peakTemp * 1.20; tg += 50) {
      tft.drawFastHLine(0, h - (tg * pxPerC) + yOffset, 160, 0xC618);
    }
    
  }

  // elapsed time
  uint16_t elapsed = (esp_timer_get_time()/1000000 -starttime_s);
  tft.setCursor(125, y);
  if(currentState != Complete)
  {
    alignRightPrefix(elapsed); 
    tft.print(elapsed);
    tft.print("s");    
  }
  

  y += MENU_ITEM_HIEGT + 2;

  tft.setCursor(2, y);
  tft.setTextColor(ST7735_BLACK, ST7735_WHITE);

  // temperature
  tft.setTextSize(2);
  alignRightPrefix((int)aktSystemTemperature);
  printfloat(aktSystemTemperature);
  tft.print("\367C");  
  tft.setTextSize(1);

  // current state
  y -= 2;
  tft.setCursor(95, y);
  tft.setTextColor(ST7735_BLACK, ST7735_GREEN);
  
  tft.print(currentStateToString());

  tft.print("        "); // lazy: fill up space

  tft.setTextColor(ST7735_BLACK, ST7735_WHITE);

  // set point
  y += 10;
  tft.setCursor(95, y);
  tft.print("Sp:"); 
  alignRightPrefix((int)heaterSetpoint); 
  printfloat(heaterSetpoint);
  tft.print("\367C  ");

  // draw temperature curves
  //
  if(currentState != Complete)
  {
    dx = ((uint16_t)(elapsed * pxPerS))%w;

    // temperature setpoint
    dy = h - (heaterSetpoint * pxPerC) + yOffset;
    tft.drawPixel(dx, dy, ST7735_BLUE);
  
    // actual temperature
    dy = h - (aktSystemTemperature * pxPerC) + yOffset;
    tft.drawPixel(dx, dy, ST7735_RED);
  }
  
  // set values
  tft.setCursor(2, 119);
  uint16_t percent=(uint16_t)powerHeater*100/256;
  alignRightPrefix(percent); 
  tft.print(percent);
  tft.print('%');

  tft.print("      \x12 "); // alternative: \x7f
  printfloat(aktSystemTemperatureRamp);
  tft.print("\367C/s    ");
}

void printBottomLine(){
  
}

void memoryFeedbackScreen(uint8_t profileId, bool loading) {
  tft.fillScreen(ST7735_GREEN);
  tft.setTextColor(ST7735_BLACK);
  tft.setCursor(10, 50);
  tft.print(loading ? "Loading" : "Saving");
  tft.print(" profile ");
  tft.print(profileId);  
}

void saveProfile(unsigned int targetProfile) {
  activeProfileId = targetProfile;

  memoryFeedbackScreen(activeProfileId, false);

  saveParameters(activeProfileId); // activeProfileId is modified by the menu code directly, this method is called by a menu action
  saveLastUsedProfile();
  
  delay(500);
}

void loadProfile(unsigned int targetProfile) {
  memoryFeedbackScreen(targetProfile, true);
  bool ok = loadParameters(targetProfile);

  if (!ok) {
    tft.fillScreen(ST7735_RED);
    tft.setTextColor(ST7735_BLACK);
    tft.setCursor(10, 50);
    tft.print("Checksum error!");
    tft.setCursor(20, 50);
    tft.print("Review profile.");
    delay(2500);
  }

  // save in any way, as we have no undo
  activeProfileId = targetProfile;
  saveLastUsedProfile();

  delay(500);
}

void makeDefaultProfile() {
  activeProfile.soakTemp     = 130;
  activeProfile.soakDuration =  80;
  activeProfile.peakTemp     = 220;
  activeProfile.peakDuration =  40;
  activeProfile.rampUpRate   =   0.80;
  activeProfile.rampDownRate =   2.0;
}
void makeDefaultPID() {
  heaterPID.Kp =  0.60; 
  heaterPID.Ki =  0.01;
  heaterPID.Kd = 19.70;
}

void getProfileKey(uint8_t profile, char * buffer){
  buffer[0]='P';
  buffer[1]=48+(profile/100)%10;
  buffer[2]=48+(profile/10)%10;
  buffer[3]=48+profile%10;
  buffer[4]=0;  
}

bool saveParameters(uint8_t profile) {
  char buffer[5];
  getProfileKey(profile, buffer);
  Serial.print("Save Profile: ");
  Serial.println(buffer);
  PREF.putBytes(buffer, (uint8_t*)&activeProfile, sizeof(Profile_t));  

  return true;
}

bool loadParameters(uint8_t profile) {
  char buffer[5];
  getProfileKey(profile, buffer);
  Serial.print("Load Profile: ");
  Serial.println(buffer);

  size_t length = PREF.getBytesLength(buffer);
  
  if(length!=sizeof(Profile_t)){
    makeDefaultProfile();  
    Serial.println("load default PROFILE!");
  }
  else
  {
    PREF.getBytes(buffer, (uint8_t*)&activeProfile, length);
  }  

  return true;
}

bool savePID() {
  PREF.putBytes("PID", (uint8_t*)&heaterPID, sizeof(heaterPID));  
  return true;
}

bool loadPID() {
  
  size_t length = PREF.getBytesLength("PID");
  
  if(length!=sizeof(heaterPID)){
    makeDefaultPID();
Serial.println("load default PID");
  }
  else
  {
    PREF.getBytes("PID", (uint8_t*)&heaterPID, length);
  }  
  return true;  
}


void factoryReset() {
  makeDefaultProfile();

  tft.fillScreen(ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(10, 50);
  tft.print("Resetting...");

  
  PREF.clear(); 

  activeProfileId = 0;
  makeDefaultProfile();

  delay(500);
}

void saveLastUsedProfile() {
  PREF.putUChar("ProfileID",activeProfileId);
}

void loadLastUsedProfile() {
  activeProfileId = PREF.getUChar("ProfileID", 0);
  loadParameters(activeProfileId);
}

void setup() {
  //Debug
  Serial.begin(115200);
  
  //LCD init
  tft.initR(INITR_BLACKTAB);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setRotation(1);
  tft.fillScreen(ST7735_WHITE);
  tft.setTextColor(ST7735_BLACK, ST7735_WHITE);

  //init rodery encoder Timer
  encodertimer=timerBegin(1, 80, true);
  timerAttachInterrupt(encodertimer, &encoderServices, true);
  timerAlarmWrite(encodertimer, 1000, true);
  timerAlarmEnable(encodertimer);
  
  //INIT Temps
  pinMode(TEMP1_CS, OUTPUT);
  pinMode(TEMP2_CS, OUTPUT);
  Temp_1.chipSelect = TEMP1_CS;
  Temp_2.chipSelect = TEMP2_CS;
  
  //init Zero crossing interrupt
  pinMode(ZEROX, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ZEROX), zeroCrossingDetected, CHANGE);
  
  //init switchPower
  pinMode(HEATER1, OUTPUT);
  digitalWrite(HEATER1,LOW);
  pinMode(HEATER2, OUTPUT);
  digitalWrite(HEATER2,LOW);
  esp_timer_create_args_t _timerConfig;
  _timerConfig.callback = (void (*)(void*))switchPower;
  _timerConfig.dispatch_method = ESP_TIMER_TASK;
  _timerConfig.name = "switchPower";
  esp_timer_create(&_timerConfig, &SwitchPowerTimer);
  esp_timer_start_once(SwitchPowerTimer, 1000000); //start in 1 secound!

  //beep
  pinMode(BUZZER,OUTPUT);
  ledcSetup(0,1000,0);

  //LEDs:
  RGBLED.begin(RGB_CLK,RGB_SDO,RGB_SDO,0);  
  setLEDRGBBColor(0,0,0);
  
  //EEPROM init
  PREF.begin("REFLOW");
  loadLastUsedProfile();
  
  //init Wifi:
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
 
  //init Webserver
  if (MDNS.begin("ReflowController")) {
      Serial.println("MDNS responder started");
  }
  server.on("/", []() {
    server.sendHeader("Cache-Control","no-cache");
    server.send(200, "text/html", ROOT_HTML);
  });
  server.on("/status", []() {
    server.sendHeader("Cache-Control","no-cache");
    char buffer[200];
    unsigned long time = (esp_timer_get_time()-cycleStartTime)/1000;
    snprintf(buffer,200,"{\"time\": %lu, \"temp\": %.2f, \"dt\": %.2f, \"setpoint\":  %.2f, \"power\": %.2f, \"state\": \"%s\"}",time,aktSystemTemperature,aktSystemTemperatureRamp,heaterSetpoint,heaterOutput*100/256,currentStateToString());
    server.send(200, "application/json", buffer);
  });
  serverAction.on("/start", []() {
    serverAction.sendHeader("Cache-Control","no-cache");
    serverAction.sendHeader("Access-Control-Allow-Origin","*");
    if(currentState == Settings)
    {
      //Start Revlow!
      myMenue.navigate(&miCycleStart);
      myMenue.invoke();
      serverAction.send(200, "text/plain", "OK");
    }
    else
    {
      serverAction.send(200, "text/plain", "ERROR");
    }
  });
  serverAction.on("/stop", []() {
    serverAction.sendHeader("Cache-Control","no-cache");
    serverAction.sendHeader("Access-Control-Allow-Origin","*");
    bool ok=false;
    if (currentState == Complete) 
    { 
      menuExit(Menu::actionDisplay); // reset to initial state
      myMenue.navigate(&miCycleStart);
      currentState = Settings;
      menuUpdateRequest = true;
      ok=true;
    }
    else if (currentState == CoolDown) 
    {
      currentState = Complete;
      ok=true;
    }
    else if (currentState > UIMenuEnd) 
    {
      currentState = CoolDown;
      ok=true;
    }
    if(ok){
      serverAction.send(200, "text/plain", "OK");      
    }
    else
    {
      serverAction.send(200, "text/plain", "ERROR");
    }
  });
  server.onNotFound([](){
    server.send(404, "text/plain", "404 :-(");
  });
  serverAction.onNotFound([](){
    serverAction.send(404, "text/plain", "404 :-(");
  });
  
  server.begin();  
  serverAction.begin();  
  
  spashscreen();

  delay(1000);

  menuExit(Menu::actionDisplay); // reset to initial state
  myMenue.navigate(&miCycleStart);
  currentState = Settings;
  menuUpdateRequest = true;
  
  
}

void displayMenusInfos()
{
  //some basic information in the menue
  // set values
  tft.setTextSize(1);
  tft.setTextColor(ST7735_BLACK, ST7735_WHITE);    
  tft.setCursor(2, 119);
  if(WiFi.status() != WL_CONNECTED)
  {                
    tft.print("PI:Not Connnected!");      
  }
  else
  {
    tft.print("IP:");          
    tft.print(WiFi.localIP());          
  }
  tft.print("         ");      
  
  tft.setCursor(115, 119);
  printfloat(aktSystemTemperature);
  tft.print("\367C   ");
}

void loop()
{
  uint64_t time_ms = esp_timer_get_time()/1000;
  static uint64_t lastrecalczerrox=time_ms;
  static uint64_t lastzeroCrossingCalc=time_ms;
  static uint64_t lastbeep=time_ms;
  static uint64_t lastreadTemp=time_ms;
  static uint64_t lastRGBupdate=time_ms;
  static uint64_t lastreadencoder=time_ms;
  static uint64_t lastDisplayUpdate=time_ms;
  static uint64_t lastControlloopupdate=time_ms;

  static uint8_t  beepcount =10;

  // --------------------------------------------------------------------------
  // Handle internet requests
  //
  server.handleClient();  
  serverAction.handleClient();  

  // --------------------------------------------------------------------------
  // Calc new Zero crossing time point
  //
  if(time_ms >= (lastrecalczerrox + RECAL_ZEROX_TIME_MS))
  {
    lastrecalczerrox = time_ms;
    boolean error=false;
    uint8_t pointer = zeroCrossingTimesPointer;

    //look at the last 10 crossings 
    pointer = (pointer - 20) & 0x1F; 

    if(zeroCrossingTimes[pointer] < (esp_timer_get_time()-10*11*1000))
    {
      error=true;
      #ifndef NOEDGEERRORREPORT
        Serial.println("No Edge detection!");        
      #endif
    }
    if(error==false)
    {  
      if((zeroCrossingTimes[(pointer+1)&0x1F] - zeroCrossingTimes[pointer]) > 5000) //syn
      {
        pointer = (pointer - 1) & 0x1F;
      }
      for (uint8_t i=0; i<19;i++)
      {
        uint32_t delta = zeroCrossingTimes[(pointer+i+1)&0x1F] - zeroCrossingTimes[(pointer+i)&0x1F];
        if(((delta>5000) && (i%2!=1)) || ((delta<5000) && (i%2!=0)))
        {
          error=true;
          Serial.println("Mssing Edge detection!");
          break;
        }    
      }
      if(error==false)
      {
        //calc average duration:
        uint32_t duration=(zeroCrossingTimes[(pointer+19)&0x1F]-zeroCrossingTimes[(pointer-1)&0x1F])/10;
        if(duration>11000 || duration<7000) 
        {
          error=true;
          Serial.println("Duration not OK! Missing edges?");
        }
        if(error==false)
        {
          //calc zero crossing point:
          uint32_t xpoint=0;
          for (uint8_t i=0; i<20;i+=2)
          {
            uint64_t a = zeroCrossingTimes[(pointer+i)&0x1F];
            uint64_t b = zeroCrossingTimes[(pointer+i+1)&0x1F];
            xpoint+=((a+b)/2)%duration;
            
          }      
          xpoint=xpoint/10;
          for (uint8_t i=0; i<20;i+=2)
          {
            int64_t a = zeroCrossingTimes[(pointer+i)&0x1F];
            int64_t b = zeroCrossingTimes[(pointer+i+1)&0x1F];
            if(abs((((a+b)/2)%duration)-xpoint)>200) //200us jitter is ok
            {
              error=true;
              Serial.println("Edgededection jitter to HIGH!");
              break;
            }
          }     
          if(error==false){
            lastzeroCrossingCalc=time_ms;
            portENTER_CRITICAL(&ZeroCrossingMutex);
            zeroCrossingDuration=duration;
            zeroCrossingPoint=xpoint;
            portEXIT_CRITICAL(&ZeroCrossingMutex);

            /*
            Serial.print(" ");
            for(int i = 0; i < (duration%10) ; i++)
              Serial.print("\t");
            Serial.println(xpoint);
            */
          }
        }          
      }
    }        
  }

  // --------------------------------------------------------------------------
  // Report Zero Crossing error
  //
  if(time_ms >= (lastzeroCrossingCalc + ZEROX_TIMEOUT_MS))
  {
    #ifndef NOEDGEERRORREPORT
      reportError("Zero Crossing Detection Timeout!");
    #endif
  }

  // --------------------------------------------------------------------------
  // Do the beep if needed
  //
  if(time_ms >= (lastbeep + 500))
  {
    static boolean isbeeping=false;
    lastbeep=time_ms;
    if(isbeeping==false)
    {
      if(beepcount > 0)
      {
        beepcount--;
        ledcAttachPin(BUZZER, 0);
        ledcWriteTone(0, 1000);
        isbeeping=true;
      }
    }
    else
    {
      ledcDetachPin(BUZZER);
      isbeeping=false;
    }
  }

  // --------------------------------------------------------------------------
  // Temp messurment and averageing
  //
  if(time_ms >= (lastreadTemp + READ_TEMP_INTERVAL_MS))
  {
    lastreadTemp+=READ_TEMP_INTERVAL_MS; //interval should be regularly
    readThermocouple(&Temp_1);
    //readThermocouple(&Temp_2);


    static float average[READ_TEMP_AVERAGE_COUNT];
    static uint8_t stats[READ_TEMP_AVERAGE_COUNT];
    static uint8_t pointer =0;

    average[pointer]=Temp_1.temperature;
    stats[pointer]=Temp_1.stat;
    pointer=(pointer+1)%READ_TEMP_AVERAGE_COUNT;

    float sum =0;
    uint8_t state_count=0;
    uint8_t stat=0;
    for (int i=0;i<READ_TEMP_AVERAGE_COUNT;i++)
    {
      sum +=average[i];
      stat &=stats[i];
      if (stats[i]) 
      {
        state_count++;
      }
    }
    
    if (state_count>READ_TEMP_AVERAGE_COUNT/2) {
        switch (stat) {
          case 0b001:
            reportError("Temp Sensor 1: Open Circuit");
            break;
          case 0b010:
            reportError("Temp Sensor 1: GND Short");
            break;
          case 0b100:
            reportError("Temp Sensor 1: VCC Short");
            break;
          default:
            reportError("Temp Sensor 1: Multiple errors!");
            break;
        }
    }

    aktSystemTemperature = sum/READ_TEMP_AVERAGE_COUNT;


    static float averagees[1000/READ_TEMP_INTERVAL_MS];
    static uint16_t p=0;

    aktSystemTemperatureRamp = aktSystemTemperature - averagees[p];

    averagees[p]=aktSystemTemperature;
    p=(p+1)%(1000/READ_TEMP_INTERVAL_MS);
    
  }

  // --------------------------------------------------------------------------
  // Show temp as RGB color and print display buttom line
  //
  if(time_ms >= (lastRGBupdate + 1000))
  {
    lastRGBupdate=time_ms;

    float t=(300-aktSystemTemperature)/300.0/2;
    setLEDRGBBColor(RGB_LED_BRITHNESS_1TO255 * Hue_2_RGB( 0, 1, t+0.33 ),RGB_LED_BRITHNESS_1TO255 * Hue_2_RGB( 0, 1, t ),RGB_LED_BRITHNESS_1TO255 * Hue_2_RGB( 0, 1, t-0.33 ));
    
    if (currentState < UIMenuEnd) 
    {
      displayMenusInfos();
    }
  }

  // --------------------------------------------------------------------------
  // handle encoder rotation
  //
  if(time_ms >= (lastreadencoder + 10))
  {
    lastreadencoder=time_ms;
    int16_t encMovement = Encoder.getValue();
    if (encMovement) 
    {
      encAbsolute += encMovement;
      if (currentState == Settings) 
      {
        myMenue.navigate((encMovement > 0) ? myMenue.getNext() : myMenue.getPrev());
        menuUpdateRequest = true;
      }
      else if (currentState == Edit) 
      {
        if (myMenue.currentItem != &Menu::NullItem) 
        {
          myMenue.executeCallbackAction(Menu::actionDisplay);      
        }        
      }
    }

  }
  // --------------------------------------------------------------------------
  // handle encoder button press
  //
  switch (Encoder.getButton()) 
  {
    case ClickEncoder::Clicked:
      if (currentState < UIMenuEnd) 
      {
        myMenue.invoke();
        menuUpdateRequest = true;
      }
      else if (currentState == Complete) 
      { 
        menuExit(Menu::actionDisplay); // reset to initial state
        myMenue.navigate(&miCycleStart);
        currentState = Settings;
        menuUpdateRequest = true;
      }
      else if (currentState == CoolDown) 
      {
        currentState = Complete;
      }
      else if (currentState > UIMenuEnd) 
      {
        currentState = CoolDown;
      }
      break;
    case ClickEncoder::DoubleClicked:
      Serial.println("DClick");
      Serial.print("currentState: ");Serial.println(currentState);
      Serial.print("myMenue.getParent(): ");Serial.println((uint32_t)myMenue.getParent());
      Serial.print("&miExit: ");Serial.println((uint32_t)&miExit);
      if (currentState == Edit) 
      {
        myMenue.executeCallbackAction(Menu::actionParent);      
      }
      else if (currentState < UIMenuEnd && myMenue.getParent() != &miExit) 
      {
        tft.fillScreen(ST7735_WHITE);
        displayMenusInfos();
        myMenue.navigate(myMenue.getParent());
        menuUpdateRequest = true;
      }
      break;
  }

  // --------------------------------------------------------------------------
  // handle menu update
  //
  if (menuUpdateRequest) 
  {
    menuUpdateRequest = false;
    uint64_t dtime=esp_timer_get_time();
    myMenue.render(renderMenuItem, MENUE_ITEMS_VISIBLE);
    Serial.print("Menue render took: ");
    Serial.print((uint32_t)(esp_timer_get_time() - dtime));
    Serial.println("us!");
  }

  // --------------------------------------------------------------------------
  // update Display
  //
  if(time_ms >= (lastDisplayUpdate + 1000))
  {
    lastDisplayUpdate=time_ms;
    if (currentState > UIMenuEnd) {
      uint64_t dtime=esp_timer_get_time();
      updateProcessDisplay();
      Serial.print("Display update took: ");
      Serial.print((uint32_t)(esp_timer_get_time() - dtime));
      Serial.println("us!");
    }
  }



  // --------------------------------------------------------------------------
  // control loop
  //
  if(time_ms >= (lastControlloopupdate + 100))
  {
    lastControlloopupdate+=100; 

    static State previousState= Idle;
    static uint64_t stateChangedTime_ms=time_ms;
    boolean stateChanged=false;
    if (currentState != previousState) 
    {
      stateChangedTime_ms=time_ms;
      stateChanged = true;
      previousState = currentState;
    }
    static float rampToSoakStartTemp;
    static float coolDownStartTemp;
    
    heaterInput = aktSystemTemperature; 

    switch (currentState) 
    {
      case RampToSoak:
        if (stateChanged) 
        {

          rampToSoakStartTemp=aktSystemTemperature;
          heaterSetpoint = rampToSoakStartTemp;

          PID.SetMode(AUTOMATIC);
          PID.SetControllerDirection(DIRECT);
          PID.SetTunings(heaterPID.Kp, heaterPID.Ki, heaterPID.Kd);
        }

        heaterSetpoint = rampToSoakStartTemp + (activeProfile.rampUpRate * (time_ms-stateChangedTime_ms)/1000.0);

        if (heaterSetpoint >= activeProfile.soakTemp) 
        {
          currentState = Soak;
        }
        break;

      case Soak:

        heaterSetpoint = activeProfile.soakTemp;

        if (time_ms - stateChangedTime_ms >= (uint32_t)activeProfile.soakDuration * 1000) 
        {
          currentState = RampUp;
        }
        break;

      case RampUp:

        heaterSetpoint = activeProfile.soakTemp + (activeProfile.rampUpRate * (time_ms-stateChangedTime_ms)/1000.0);

        if (heaterSetpoint >= activeProfile.peakTemp) 
        {
          currentState = Peak;
        }
        break;

      case Peak:

        heaterSetpoint = activeProfile.peakTemp;

        if (time_ms - stateChangedTime_ms >= (uint32_t)activeProfile.peakDuration * 1000) {
          currentState = CoolDown;
        }
        break;

      case CoolDown:
        if (stateChanged) {
          PID.SetMode(MANUAL);

          beepcount=3;  //Beep! We need the door open!!!

          //rampDown from the last setpoint
          coolDownStartTemp=heaterSetpoint;
        }

        heaterSetpoint = coolDownStartTemp - (activeProfile.rampDownRate * (time_ms - stateChangedTime_ms) / 1000.0);

        if (heaterSetpoint < IDLE_TEMP) {
            heaterSetpoint = IDLE_TEMP;
        }
        
        if (aktSystemTemperature < IDLE_TEMP && heaterSetpoint == IDLE_TEMP) {
          currentState = Complete;
          PID.SetMode(MANUAL);

          beepcount=1;  //Beep! We are done!!!

        }
        break;
      case Tune:
        if (stateChanged) 
        {


          PID.SetMode(MANUAL);
          
          PIDTune.Cancel();
          heaterOutput = 255*tuningHeaterOutput/100;
          PIDTune.SetNoiseBand(tuningNoiseBand);
          PIDTune.SetOutputStep(255*tuningOutputStep/100);
          PIDTune.SetLookbackSec(tuningLookbackSec);
        }
        
        heaterSetpoint = aktSystemTemperature;

        int8_t val = PIDTune.Runtime();

        if (val != 0) {
          currentState = CoolDown;
          heaterPID.Kp = PIDTune.GetKp();
          heaterPID.Ki = PIDTune.GetKi();
          heaterPID.Kd = PIDTune.GetKd();

          savePID();

          tft.setCursor(40, 40);
          tft.print("Kp: "); tft.print((uint32_t)(heaterPID.Kp * 100));
          tft.setCursor(40, 52);
          tft.print("Ki: "); tft.print((uint32_t)(heaterPID.Ki * 100));
          tft.setCursor(40, 64);
          tft.print("Kd: "); tft.print((uint32_t)(heaterPID.Kd * 100));
        }

        break;
    }

    PID.Compute();

    if (
         currentState == RampToSoak ||
         currentState == Soak ||
         currentState == RampUp ||
         currentState == Peak ||
         currentState == Tune          
       )
    {
  
      if (heaterSetpoint+100 < aktSystemTemperature) // if we're 100 degree cooler than setpoint, abort
      {
        reportError("Temperature is Way to HOT!!!!!"); 
      }
      //make it more linear!
      powerHeater = asinelookupTable[(uint8_t)heaterOutput]; 
    } 
    else if(currentState == Edit && myMenue.currentItem==&miManual)
    {
      powerHeater=(encAbsolute*255)/100;
      Serial.print("Manual Heating:");Serial.println(powerHeater);
    }
    else
    {
      powerHeater =0;
    }
  }

}
