/* 
 * This code was originally taken from:
 *
 * https://makezine.com/projects/led-nixie-display/
 * 
 * I then ported it to the Particle Photon (I am Donnie Barnes <djb@donniebarnes.com> and started adding a few features.
 *
 * And then I built a couple complete clocks and gave one to my friend Caroline, who continued to add features, including
 * the different modes and now better DST calculation.
 *
 */
#include <neopixel.h>
#include <Sunrise.h>

#include <math.h>


const int FIRST_PANEL_INDEX = 0;
const int SECOND_PANEL_INDEX = 1;
const int THIRD_PANEL_INDEX = 2;
const int FOURTH_PANEL_INDEX = 3;


const uint8_t PIN = D0;       // Datenpin. ca. 220-470 Ohm Widerstand zusaetzlich!
                             // set the data output pin here and use a 220-470 ohm resistor in-line to the data input on the LED PCB
const uint8_t DIGITS = 4;    // Anzahl Digits à 20 LEDs
                             // Number of digits of the clock, each with 20 LEDs

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(DIGITS * 20, PIN, WS2812B);     // Konstruktor WS2812
                                                                                          // initialize our LEDs for communication

struct RGB
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct RGB overnightDisplayColor; //color to display overnight -- low intensity so it won't wake anyone up
struct RGB* hourDisplayColor; //color to display the hour numbers in
struct RGB* minuteDisplayColor; //color to display the minute numbers in
struct RGB* secondDisplayColor; //color to display the second numbers in
struct RGB sunsetColor; //color to display the second numbers in
struct RGB carolinaBlueColor;

Sunrise* sunrise;

bool showSeconds;
bool stayUpLateMode;

enum DisplayMode { ALWAYS_BRIGHT, ALWAYS_DARK, SUNRISE_SET_RED, STAY_UP_LATE };
DisplayMode currentMode;
int stayUpLate_startBrightTime;
int stayUpLate_endBrightTime;

double localLatitudeInDegrees;
double localLongitudeInDegrees;

int localTimeZoneInStandardTime;
bool isDST;
int sunsetTime;
int sunriseTime;

bool dailyInfoNeedsUpdating;
int dayOfTheWeek;

/*
 * @brief   Ausgabe einer Ziffer auf einerm Digit
 *          Output of a digit on one digit (?)
 * @param   Digit (ab 0) Pretty sure this is which digit POSITION do you want
 *          Zahl (0..9)  Value of that digit
 *          Farbe (RGB)  Color to use
 * @return  none
 * 
 */
void displayNumberOnPanel (uint8_t digit, uint8_t indexOfPanel, struct RGB* inputColor)
{
  pixels.setPixelColor((digit * 20) + (indexOfPanel * 2), pixels.Color (inputColor->red, inputColor->green, inputColor->blue));
  pixels.setPixelColor((digit * 20) + (indexOfPanel * 2) + 1, pixels.Color (inputColor->red, inputColor->green, inputColor->blue));
}

/*
 * @brief   convert a number like 25 to the two digits two and five and output them to the panel
 * @param   inputTimeInNumberForm (0-65536) // Time as input number
 *          inputColor // input color (RGB struct) 
 *          leadingZero // Show leading zeros (0=nope, 1=yep)
 *          pulseIn // Pulse this motherfucker? true=yes, false=no
 * @return  none
 */
void setTheTimeOneColor (uint16_t inputTimeInNumberForm, struct RGB* inputColor, uint8_t leadingZero, bool pulseIn) 
{
  uint8_t panelIndex=0;
  uint8_t i=0;

  if (pulseIn)
  {
    for (i = 0; i <= 128; i++)
    {
      pixels.setBrightness(255 - i*2);
      pixels.show();
    }
  }
  
  pixels.clear();   // clear all panels
                    

  if (((inputTimeInNumberForm >= 10000) || leadingZero) && DIGITS >= 5)    // First panel
  {
    displayNumberOnPanel (panelIndex, (inputTimeInNumberForm / 10000) % 10, inputColor);
    panelIndex++;
  }
  
  if (((inputTimeInNumberForm >= 1000) || leadingZero) && DIGITS >= 4)    // Second panel
  {
    displayNumberOnPanel (panelIndex, (inputTimeInNumberForm / 1000) % 10, inputColor);
    panelIndex++;
  }
  
  if (((inputTimeInNumberForm >= 100) || leadingZero) && DIGITS >= 3)    // Third panel
  {
    displayNumberOnPanel (panelIndex, (inputTimeInNumberForm / 100) % 10, inputColor);
    panelIndex++;
  }
  
  if (((inputTimeInNumberForm >= 10) || leadingZero) && DIGITS >= 2)    // Fourth panel
  {
    displayNumberOnPanel (panelIndex, (inputTimeInNumberForm / 10) % 10, inputColor);
    panelIndex++;
  }
  
  displayNumberOnPanel (panelIndex, inputTimeInNumberForm % 10, inputColor);       // Fifth panel

  pixels.setBrightness(255); //huh?
  pixels.show();
}

void setTheTime(int hour, struct RGB* inputHourColor, int minute, struct RGB* inputMinuteColor, int seconds = 0, struct RGB* inputSecondColor = NULL){
    if (hour >= 10) {
        displayNumberOnPanel (FIRST_PANEL_INDEX, (hour / 10) % 10, inputHourColor);
    } 
    displayNumberOnPanel (SECOND_PANEL_INDEX, hour % 10, inputHourColor);
        
    if (minute >= 10){ 
        displayNumberOnPanel (THIRD_PANEL_INDEX, (minute / 10) % 10, inputMinuteColor);
    } else { 
        //this will display '0' on the third panel if the minute is before 10
        displayNumberOnPanel (THIRD_PANEL_INDEX, 0, inputMinuteColor);
    }
    displayNumberOnPanel (FOURTH_PANEL_INDEX, minute % 10, inputMinuteColor);

    if (showSeconds && inputSecondColor != NULL) {
        if (seconds >= 10){
            displayNumberOnPanel (THIRD_PANEL_INDEX, (seconds / 10) % 10, inputSecondColor);
        } else { 
            //this will display '0' on the third panel if the second is before 10
            displayNumberOnPanel (THIRD_PANEL_INDEX, 0, inputSecondColor);
        }
        displayNumberOnPanel (FOURTH_PANEL_INDEX, seconds % 10, inputSecondColor);
    }
}

bool isDSTActive()
{ 
  int dayOfMonth = Time.day();
  int month = Time.month();
  int dayOfWeek = Time.weekday(); 

  if (month >= 4 && month <= 9)
  { // April to September definetly DST
    return true;
  }
  else if (month < 3 || month > 10)
  { // before March or after October is definetly standard time
    return false;
  }

  // March and October need deeper examination
  bool lastSundayOrAfter = (dayOfMonth - dayOfWeek > 24);
  if (!lastSundayOrAfter)
  { // before switching Sunday
    return (month == 10); // October DST will be true, March not
  }

  if (dayOfWeek)
  { // AFTER the switching Sunday
    return (month == 3); // for March DST is true, for October not
  }

  int secSinceMidnightUTC = Time.now() % 86400;
  bool dayStartedAs = (month == 10); // DST in October, in March not
  // on switching Sunday we need to consider the time
  if (secSinceMidnightUTC >= 1*3600)
  { // 1:00 UTC (=1:00 GMT/2:00 BST or 2:00 CET/3:00 CEST)
    return !dayStartedAs;
  }

  return dayStartedAs;
}

void updateDailyInfo(){
    dayOfTheWeek = Time.day();
    
    int localTimeZone = localTimeZoneInStandardTime;
    isDST = isDSTActive();
    if (isDST == 1) {
        localTimeZone = localTimeZoneInStandardTime + 1;
    } 
    
    Time.zone(localTimeZone);
    
    int sunsetTimeInMin = sunrise->Set(Time.month(), Time.day());
    int sunsetTimeHour = int(floor(sunsetTimeInMin / 60));
    int sunsetTimeMinute = sunsetTimeInMin % 60;
    sunsetTime = (sunsetTimeHour*100) + sunsetTimeMinute;
    
    int sunriseTimeInMin = sunrise->Rise(Time.month(), Time.day());
    int sunriseTimeHour = int(floor(sunriseTimeInMin / 60));
    int sunriseTimeMinute = sunriseTimeInMin % 60;
    sunriseTime = (sunriseTimeHour*100) + sunriseTimeMinute;
}

float getLatitudeInDegrees(){
    return localLatitudeInDegrees;
}

void setLatitudeInDegrees(float value){
    localLatitudeInDegrees = value;
}

float getLongitudeInDegrees(){
    return localLongitudeInDegrees;
}

void setLongitudeInDegrees(float value){
    localLongitudeInDegrees = value;
}

int getTimeZoneInStandardTime(){
    return localTimeZoneInStandardTime;
}

struct RGB calculateSunriseColor(int offsetFromSunriseInMinutes){
    struct RGB outputColor;
    return outputColor;
}

struct RGB calculateSunsetColor(int offsetFromSunsetInMinutes){
    struct RGB outputColor;
    return outputColor;
}

void setTimeZoneInStandardTime(int value){
    localTimeZoneInStandardTime = value;
}

void setHourColor(struct RGB* inputColor){
    hourDisplayColor->red = inputColor->red;
    hourDisplayColor->green = inputColor->green;
    hourDisplayColor->blue = inputColor->blue;
}

void setMinuteColor(struct RGB* inputColor){
    minuteDisplayColor->red = inputColor->red;
    minuteDisplayColor->green = inputColor->green;
    minuteDisplayColor->blue = inputColor->blue;
}

void setSecondColor(struct RGB* inputColor){
    secondDisplayColor->red = inputColor->red;
    secondDisplayColor->green = inputColor->green;
    secondDisplayColor->blue = inputColor->blue;
}

void setDisplayMode(DisplayMode mode_in){
    currentMode = mode_in;
}

void setup() {
    dayOfTheWeek = Time.day();
    showSeconds = false;
    stayUpLateMode = true;
    isDST = Time.isDST();
    currentMode = SUNRISE_SET_RED;

    localLatitudeInDegrees = 35.9101;
    localLongitudeInDegrees = -79.0753;
    localTimeZoneInStandardTime = -5;
    
    pixels.begin();
    
    overnightDisplayColor.red = 3;
    overnightDisplayColor.green = 7;
    overnightDisplayColor.blue = 10;
    
    sunsetColor.red = 150;
    sunsetColor.green = 0;
    sunsetColor.blue = 0;
    
    carolinaBlueColor.red = 67;
    carolinaBlueColor.green = 150;
    carolinaBlueColor.blue = 199;
    
    sunrise = new Sunrise(localLatitudeInDegrees, localLongitudeInDegrees, localTimeZoneInStandardTime);
    hourDisplayColor = new struct RGB;
    minuteDisplayColor = new struct RGB;
    secondDisplayColor = new struct RGB;
    
    updateDailyInfo();
}

void loop() {
    
    if (dayOfTheWeek != Time.day()){
        updateDailyInfo();
    }
    
    bool debug = false;

    int currentTime = (Time.hour()*100)+Time.minute();
    //if (debug){
        //setTheTimeOneColor(sunriseTime, &sunsetColor, 1, false);
    //}
    if (currentMode == SUNRISE_SET_RED){
        if (currentTime < (sunriseTime - 30) || currentTime > sunsetTime){// sunsetTime){
        //if it's after sunset and before sunrise, display at low intensity
            hourDisplayColor = &overnightDisplayColor;
            minuteDisplayColor = &overnightDisplayColor;
            secondDisplayColor = &overnightDisplayColor;
        } else if ((currentTime >= (sunriseTime - 30)) && (currentTime < sunriseTime)){
        // if it's within 30 min of sunrise, color goes red
            hourDisplayColor = &sunsetColor;
            minuteDisplayColor = &sunsetColor;
            secondDisplayColor = &sunsetColor;
        } else if ((currentTime <= sunsetTime) && (currentTime > (sunsetTime - 30))){
        // if it's within 30 min of sunset, color goes red
            hourDisplayColor = &sunsetColor;
            minuteDisplayColor = &sunsetColor;
            secondDisplayColor = &sunsetColor;
        } else {
        //we get here if it's daytime
            hourDisplayColor = &carolinaBlueColor;
            minuteDisplayColor = &carolinaBlueColor;
            secondDisplayColor = &carolinaBlueColor;
        }
    } else if(currentMode == ALWAYS_BRIGHT){
        //always show the bright color
        hourDisplayColor = &carolinaBlueColor;
        minuteDisplayColor = &carolinaBlueColor;
        secondDisplayColor = &carolinaBlueColor;
    } else if(currentMode == ALWAYS_DARK){
        //always show the dark color
        hourDisplayColor = &overnightDisplayColor;
        minuteDisplayColor = &overnightDisplayColor;
        secondDisplayColor = &overnightDisplayColor;
    } else if (currentMode == STAY_UP_LATE){
        if (currentTime < 700 || currentTime > 2100){// sunsetTime){
        //if it's after 9pm and before 7am, display at low intensity
            hourDisplayColor = &overnightDisplayColor;
            minuteDisplayColor = &overnightDisplayColor;
            secondDisplayColor = &overnightDisplayColor;
        } else {
        //we get here if it's between 7am and 9pm
            hourDisplayColor = &carolinaBlueColor;
            minuteDisplayColor = &carolinaBlueColor;
            secondDisplayColor = &carolinaBlueColor;
        }
    }
    
    pixels.clear();   // clear all panels
                    
    setTheTime(Time.hourFormat12(), hourDisplayColor, Time.minute(), minuteDisplayColor, Time.second(), secondDisplayColor);

    //pixels.setBrightness(255); //this will flash when something changes; I commented it out now that we're showing seconds
    pixels.show();
    
    delay(250);

}
