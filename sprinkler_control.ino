/*

Arduino sprinkler system.

Pin numbers correspond to Arduino Uno.
RTC is adafuit part number 264
LCD is 2 line, 16 characters
Use with a relay board like this: http://www.amazon.com/dp/B00KTELP3I/ref=pe_1098610_137716200_cm_rv_eml_rv0_dp

0  right button  (run other button lead to 0V)
1  left button   (run other button lead to 0V)
2  LCD D7
3  LCD D6
4  LCD D5
5  LCD D4
6  LCD enable
7  LCD RS
8  Zone1 relay 
9  Zone2 relay
10 Zone3 relay
11 Zone4 relay
12 Zone5 relay
13 enter button    (run other button lead to 0V)
A0
A1
A2 RTC 0V (digital 16 in code)
A3 RTC 5V (digital 17 in code)
A4 RTC SDA
A5 RTC SCL

EEPROM memory assigned as follows:
rain delay
0: rain delay expiration day
1: rain delay expiration month
2: rain delay expiration year-2000

zone 1
slots 10-19
10: HH
11: MM
12: duration
13: frequency
14: next run day
15: shutoff HH
16: shutoff MM
17: next run month
18: next run year-2000

zone 2
slots 20-29
20: HH
21: MM
22: duration
23: frequency
24: next run day
25: shutoff HH
26: shutoff MM
27: next run month
28: next run year-2000

etc...

TODO: code should really be written to use unix time instead of day month year...  This would be a 
      fairly major rewrite however.
TODO: code needs to be able to schedule the next watering after an extended power outage.

*/

// include the library code:
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <Wire.h>
#include "RTClib.h"

//define rtc
RTC_DS1307 rtc;

//initialize LCD library with pin #'s
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

//set pin name's
int LEFT = 1;  //left button pin
int RIGHT = 0;  //right button pin
int ENTER = 13;  //enter button pin

int zone1 = 8;   //zone1 relay pin
int zone2 = 9;   //zone2 relay pin
int zone3 = 10;   //zone3 relay pin
int zone4 = 11;   //zone4 relay pin
int zone5 = 12;  //zone5 relay pin

int RTC0V = 16;  //RTC ground
int RTC5V = 17;  //RTC 5V

//initialize global variables
DateTime now;
DateTime displayedDateTime;
unsigned long time1 = 0;  //used to time-out menu's
unsigned long time2 = 0;  //used to time-out menu's
const int numberOfZones = 5;  //number of zones for controller to control.
boolean refreshHome = true;  //used to control home screen refreshing

//status message
boolean zoneStatus[numberOfZones];

//EEPROM address names. 's' = 'scheduled'
const int sHH = 0;
const int sMM = 1;
const int sDur = 2;
const int sFreq = 3;
const int sDay = 4;
const int sOffHH = 5;
const int sOffMM = 6;
const int sMonth = 7;
const int sYear = 8;

//custom LCD symbols (for zone status message)
byte block[8] = {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111}; //indicates zone ON
byte box[8] = {B11111, B10001, B10001, B10001, B10001, B10001, B10001, B11111,}; //indicates zone OFF

void setup(){
  // setup the LCD's number of columns and rows: 
  lcd.begin(16, 2);
  lcd.clear();
  
  //setup button pins w/ built in pull-up resistors
  pinMode(RIGHT, INPUT_PULLUP);
  pinMode(LEFT, INPUT_PULLUP);
  pinMode(ENTER, INPUT_PULLUP);

  //setup zone pins and write HIGH.
  //relays trigger when LOW
  //pins set HIGH prior to defining pinMode to prevent relay switching at startup/reset.
  digitalWrite(zone1, HIGH); pinMode(zone1, OUTPUT); 
  digitalWrite(zone2, HIGH); pinMode(zone2, OUTPUT); 
  digitalWrite(zone3, HIGH); pinMode(zone3, OUTPUT); 
  digitalWrite(zone4, HIGH); pinMode(zone4, OUTPUT); 
  digitalWrite(zone5, HIGH); pinMode(zone5, OUTPUT); 

  //setup analog pins for RTC power
  pinMode(RTC0V, OUTPUT); digitalWrite(RTC0V, LOW);
  pinMode(RTC5V, OUTPUT); digitalWrite(RTC5V, HIGH);

  //start I2C and start RTC
  Wire.begin();
  rtc.begin();

  //initialize zoneStatus boolean array
  int i;
  for(i = 0; i < numberOfZones; i++){
    zoneStatus[i] = false;
  }

  //create custom LCD characters (for zone status)
  lcd.createChar(6, box);
  lcd.createChar(7, block);

  //if RTC is not running, set it's time to time this sketch was compiled.
  //TODO: should probably add a "set the clock" option to the menu...
  if(! rtc.isrunning()){
    lcd.clear();
    lcd.print("RTC NOT running!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    delay(2000);
  }
  // This line sets the RTC with an explicit date & time, for example to set
  // January 21, 2014 at 3am you would call:
  // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));

  //NOTE: do not enable serial communication!  buttons wired to pins 0 and 1!!
  
}

void loop(){
  //check for zones to turn on or off
  activateScheduledZones();
  deactivateScheduledZones();

  //draw home screen
  int zoneNumber = 1;
  now = rtc.now();
  if(refreshHome){
    //display current date
    displayedDateTime = now;
    lcd.clear();
    updateDisplayedDate();
    //display current time
    lcd.setCursor(9,0);
    updateDisplayedTime(now.hour(), now.minute());
    lcd.setCursor(0,1);
    //display zone status
    updateStatus();
    for(zoneNumber = 1; zoneNumber <= numberOfZones; zoneNumber ++){
      switch(zoneStatus[zoneNumber - 1]){
        case true:
        lcd.write(byte(7));  //display solid square
        break;
        case false:
        lcd.write(byte(6));  //display square outline
        break;
      }
    }
    refreshHome = false;
  }

  //if home screen is already drawn, just update the time and date and message as required.
  if(! refreshHome){
    //update time
    if(displayedDateTime.minute() != now.minute()){
      lcd.setCursor(9,0);
      updateDisplayedTime(now.hour(), now.minute());
      displayedDateTime = now;  
    }
    //update date
    if(displayedDateTime.day() != now.day()){
      lcd.setCursor(0,0);
      updateDisplayedDate();
      displayedDateTime = now;
    }
    //udpate status
    for(zoneNumber = 1; zoneNumber <= numberOfZones; zoneNumber ++){
      if(digitalRead(zoneNumber + 7) == zoneStatus[zoneNumber-1]){
        zoneStatus[zoneNumber-1] = !zoneStatus[zoneNumber-1];
        lcd.setCursor(zoneNumber - 1, 1);
        switch(zoneStatus[zoneNumber-1]){
          case true:
          lcd.write(byte(7));
          break;
          case false:
          lcd.write(byte(6));
          break;
        }
      }
    }
  }

 //prevents time-out problems in menus  ...somehow
  time1 = millis();
  time2 = millis();

  //TODO: change LEFT and RIGHT to cycle through zone status messages
  //enter menu if ENTER button is pressed and released.
  if(digitalRead(ENTER) == 0){
    while(digitalRead(ENTER) == 0){}
    refreshHome = true;  //next time loop() iterates it will re-draw the home screen
    menu();
  }

} //loop()

void updateStatus(){
  int zoneNumber;
  //update zoneStatus array
  for(zoneNumber = 1; zoneNumber <= numberOfZones; zoneNumber++){
    if(digitalRead(zoneNumber + 7) == LOW){  //relay pins 8-12
      zoneStatus[zoneNumber - 1] = true;
    }else{
      zoneStatus[zoneNumber - 1] == false;
    }
  }
}

void menu(){
  time1 = millis(); //used to time out menu()
  time2 = millis(); //used to time out menu()
  boolean updateDisplay = true;  //eliminate screen flicker
  int menuItem = 1; //current menu item
  int menuItems = 5;  //total # of menu items

  //TODO: add 'view next scheduled' menu option?
  
  //menu() runs inside loop that times out back to loop() after 20 seconds.
  while(time1 < time2 + 20000){
    //display menu item on first pass through loop and after LEFT/RIGHT button presses
    if(updateDisplay == true){
      lcd.clear();
      switch(menuItem){
        case 1:
        lcd.print("Shutoff all zone");  
        lcd.setCursor(0,1);
        lcd.print("<HOME-YES-NEXT>");
        break;
        case 2:
        lcd.print("Rain delay?");
        lcd.setCursor(0,1);
        lcd.print("<HOME-YES-NEXT>");
        break;
        case 3:
        lcd.print("Water a zone?");
        lcd.setCursor(0,1);
        lcd.print("<HOME-YES-NEXT>");
        break;
        case 4:
        lcd.print("See schedule?");
        lcd.setCursor(0,1);
        lcd.print("<HOME-YES-NEXT>");
        break;
        case 5:
        lcd.print("Program a zone?");
        lcd.setCursor(0,1);
        lcd.print("<HOME-YES-NEXT>");
        break;
      }
      updateDisplay = false;
    }
  //go back to loop() if LEFT is pressed and released.
  if(digitalRead(LEFT) == 0){
    while(digitalRead(LEFT) == 0){}
    break;
    }
  //go to next menuItem if RIGHT is pressed and released.
  if(digitalRead(RIGHT) == 0){
    while(digitalRead(RIGHT) == 0){}
    if(menuItem >= menuItems){
      menuItem = 1;
      }else{menuItem = menuItem + 1;}
      updateDisplay = true;
    }
  //execute menuItem if ENTER is pressed and released.
  if(digitalRead(ENTER) == 0){
    while(digitalRead(ENTER) == 0){}
    if(menuItem == 1){
      shutoffAllZones();
      lcd.clear();
      lcd.print("Shutdown!!");
      delay(2000);
    }
    if(menuItem == 2) rainDelay(); 
    if(menuItem == 3) waterZone(selectZone());
    if(menuItem == 4) seeSchedule();
    if(menuItem == 5) programZone(selectZone());
    break;
    }
  time1 = millis();
  
  //check for zones to turn on or off
  activateScheduledZones();
  deactivateScheduledZones();
  
  } // while timeout loop
  
} //menu

int selectZone(){
  int selectedZone = 0;
  //promt user to select zone
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("Select a zone");
  lcd.setCursor(7,1);
  lcd.print("-");
  lcd.setCursor(7,1);
  lcd.blink();

  time1 = millis();
  time2 = millis();
  while(time1 < time2 + 20000){
    //LEFT button action:
    if(digitalRead(LEFT) == 0){
      while(digitalRead(LEFT) == 0){}
      if(selectedZone <= 1){
        selectedZone = numberOfZones;
      }else{selectedZone = selectedZone - 1;}
      lcd.noBlink();
      lcd.setCursor(7,1);
      lcd.print(selectedZone);
      time2 = millis();
      }
    //RIGHT button action:
    if(digitalRead(RIGHT) == 0){
      while(digitalRead(RIGHT) == 0){}
      if(selectedZone == numberOfZones){
        selectedZone = 1;
      }else{selectedZone = selectedZone + 1;}
      lcd.noBlink();
      lcd.setCursor(7,1);
      lcd.print(selectedZone);
      time2 = millis();
      }
    //ENTER button action:
    if(digitalRead(ENTER) == 0){
      while(digitalRead(ENTER) == 0){}
      return selectedZone;
      }
    time1 = millis();
    
    //check for zones to turn on or off
    activateScheduledZones();
    deactivateScheduledZones();
    
    }
  return 0; //return "0" if time-out.
  }

void shutoffAllZones(){
  //Shutoff all zones
  digitalWrite(zone1, HIGH);
  digitalWrite(zone2, HIGH);
  digitalWrite(zone3, HIGH);
  digitalWrite(zone4, HIGH);
  digitalWrite(zone5, HIGH);
}

void rainDelay(){
  int days = 0;
  int zoneNumber = 1;
  DateTime newSchedule;
  
  //promt for length of rain delay (in days)
  lcd.clear();
  lcd.print(" Delay watering");
  lcd.setCursor(3,1);
  lcd.print("- days.");
  lcd.setCursor(3,1);
  lcd.blink();

  time1 = millis();
  time2 = millis();

  while(time1 < time2 + 20000){
    //left button action
    if(digitalRead(LEFT) == 0){
      while(digitalRead(LEFT) == 0){}
      if(days <= 1){
        days = 7;
      }else{
        days = days - 1;
      }
      lcd.noBlink();
      lcd.print(days);
      lcd.setCursor(3,1);
    }
    //RIGHT button
    if(digitalRead(RIGHT) == 0){
      while(digitalRead(RIGHT) == 0){}
      if(days >= 7){
        days = 1;
      }else{
        days = days + 1;
      }
      lcd.noBlink();
      lcd.print(days);
      lcd.setCursor(3,1);
    }
    //ENTER button
    if(digitalRead(ENTER) == 0){
      while(digitalRead(ENTER) == 0){}
      //turn off all zones
      shutoffAllZones();
      
      //update scheduled days to reflect rain delay
      for(zoneNumber = 1; zoneNumber <= numberOfZones; zoneNumber++){
        now = rtc.now();
        DateTime currentSchedule ((int)EEPROM.read(zoneNumber * 10 + sYear) + 2000,
                                  (int)EEPROM.read(zoneNumber * 10 + sMonth),
                                  (int)EEPROM.read(zoneNumber * 10 + sDay),
                                  (int)EEPROM.read(zoneNumber * 10 + sHH),
                                  (int)EEPROM.read(zoneNumber * 10 + sMM),0);
        if(currentSchedule.unixtime() < now.unixtime() + 86400L * days){
          newSchedule = now + TimeSpan(days,0,0,0);
          EEPROM.update(zoneNumber * 10 + sYear, newSchedule.year());
          EEPROM.update(zoneNumber * 10 + sMonth, newSchedule.month());
          EEPROM.update(zoneNumber * 10 + sDay, newSchedule.day());
        }
      }
      //confirm delay with user
      //TODO: list zones that were delayed.
      //TODO: show rain delay message on home screen
      //TODO: update rain delay info in EEPROM
      lcd.clear();
      lcd.print(days);
      lcd.print(" day delay");
      lcd.setCursor(0,1);
      lcd.print("confirmed");
      delay(3000);
      return; 
    }
  time1 = millis();
  }
}

void waterZone(int zoneNumber){
  //handle selectZone() timing out and passing "0"
  if(zoneNumber == 0) return;

  int duration = 10; //how long to water in minutes
  DateTime shutOffTime;

  //have user select duration
  if(selectDuration(&duration) == false) return; //cancel back to menu() if selectDuration times out.
  //water 'zoneNumber' for 'duration'
  digitalWrite(zoneNumber + 7, LOW);  //zone relay pins 8-12
  //update EEPROM shutoff time so watering will end after 'duration'.
  now = rtc.now();
  shutOffTime = now + TimeSpan(0, 0, duration, 0);
  EEPROM.update(zoneNumber * 10 + sOffHH, shutOffTime.hour());
  EEPROM.update(zoneNumber * 10 + sOffMM, shutOffTime.minute());
  //display "watering zone 'zoneNumber' "
  lcd.clear();
  lcd.print("Watering zone ");
  lcd.print(zoneNumber);
  lcd.setCursor(0,1);
  lcd.print("for ");
  lcd.print(duration);
  lcd.print(" minutes");
  //pause for 2 seconds.
  time1 = millis();
  time2 = millis();
  while(time1 < time2 + 2000){
    //check for zones to turn on or off
    activateScheduledZones();
    deactivateScheduledZones();
    time1 = millis();
  }
}

void programZone(int zoneNumber){
  //handle selectZone() timing out and passing "0"
  if(zoneNumber == 0) return;
  
  //get initial values for these variables from EEPROM
  int HH = EEPROM.read(zoneNumber * 10 + sHH);  //start time HH
  int MM = EEPROM.read(zoneNumber * 10 + sMM);   //start time MM
  int duration = EEPROM.read(zoneNumber * 10 + sDur);  //duration time in minutes.
  int frequency = EEPROM.read(zoneNumber * 10 + sFreq);  //frequency in days. (2 = water every two days.)
  //handle EEPROM addresses not in the correct range.
  if(HH <= 0 || HH > 24) HH = 12;
  if(MM < 0 || HH > 59) MM = 0;
  if(duration <= 0 || duration >60) duration = 10;
  if(frequency < 1 || frequency > 7) frequency = 2;

  //These fuctions return false if they time out
  //and cause programZone to return to loop().
  if(! selectStartTime(&HH, &MM)) return;
  if(! selectDuration(&duration)) return;
  if(! selectFrequency(&frequency)) return;

  //Display start time, duration, and frequency confirmation.
  //This code runs inside a loop that times out back to loop() after 40 seconds.
  time1 = millis();  //used to time-out the confirmation dialog after a few seconds
  time2 = millis();  //used to time-out the confirmation dialog after a few seconds
  boolean updateDisplay = true;  //used to eliminate screen refresh flicker
  boolean confirm = false;  //used to toggle user confirmation.
  while(time1 < time2 + 40000){
    //Display start time, duration, frequency confirmation.  only do this once.
    if(updateDisplay == true){
      lcd.clear();
      lcd.setCursor(1,0);
      lcd.print("CONFIRM?");
      lcd.setCursor(0,1);
      updateDisplayedTime(HH,MM);
      lcd.print("  :");
      if(duration < 10){
        lcd.print("0");
        lcd.print(duration);
      }else{lcd.print(duration);}
      lcd.print("  ");
      lcd.print(frequency);
      lcd.print("DAY");
      lcd.setCursor(11,0);
      if(confirm == true) lcd.print("YES");
      if(confirm == false) lcd.print("NO");
      lcd.blink();
      updateDisplay = false;
    } //update screen once if statement
      
  //switch confirmation from NO to YES (or vis a versa) if RIGHT or LEFT button is pressed.
  if(digitalRead(LEFT) == 0 || digitalRead(RIGHT) == 0){
    while(digitalRead(LEFT) == 0 || digitalRead(RIGHT) == 0){}
    confirm = !confirm;
    lcd.noBlink();
    lcd.setCursor(11,0);
    lcd.print("   ");
    lcd.setCursor(11,0);
    if(confirm == true) lcd.print("YES");
    if(confirm == false) lcd.print("NO");
    lcd.blink();
    time2 = millis();
  } //RIGHT and LEFT buttons
  
  //save values to EEPROM if confirm == yes and ENTER is pressed.
  if(digitalRead(ENTER) == 0){
    while(digitalRead(ENTER) == 0){}
    if(confirm == true){
      //update EEPROM values
      EEPROM.update(zoneNumber * 10 + sHH, HH);
      EEPROM.update(zoneNumber * 10 + sMM, MM);
      EEPROM.update(zoneNumber * 10 + sDur, duration);
      EEPROM.update(zoneNumber * 10 + sFreq, frequency);
      //display programing success
      //verify values in EEPROM are correct first.
      if(EEPROM.read(zoneNumber * 10 + sHH) == HH &&
         EEPROM.read(zoneNumber * 10 + sMM) == MM &&
         EEPROM.read(zoneNumber * 10 + sDur) == duration &&
         EEPROM.read(zoneNumber * 10 + sFreq) == frequency &&
         scheduleNextWatering(zoneNumber)){ //stores next watering time.day in EEPROM
          lcd.noBlink();
          lcd.clear();
          lcd.print(" Programing");
          lcd.setCursor(0,1);
          lcd.print(" successfull!");
          delay(2000); //shouldn't cause problems with schedule checking
          break;
          }else{
            lcd.noBlink();
            lcd.clear();
            lcd.print("Unsuccesful");
            lcd.setCursor(2,1);
            lcd.print("Try again  :-(");
            delay(2000); //shouldn't cause problems with schedule checking
            break;
            } //else unsuccesful
      
    }else{
      lcd.noBlink();
      lcd.clear();
      lcd.print("Programming");
      lcd.setCursor(0,1);
      lcd.print("canceled");
      delay(2000);  //shouldn't cause problems with schedule checking
      break;
      }  //if confirm == true
  
    } //if digitalRead(ENTER) == 0
  
  time1 = millis();
  //check for zones to turn on or off
  activateScheduledZones();
  deactivateScheduledZones();
  } //time-out loop

} //programZone()

void seeSchedule(){
  //initialize variables for this function
  int month;
  int zoneNumber = 1;
  boolean updateDisplay = true;

  time1 = millis();
  time2 = millis();  
  while(time1 < time2 + 40000){
    now = rtc.now();
    if(updateDisplay){
      //dispaly "Z 'zoneNumber' "
      lcd.clear();
      lcd.noBlink();
      lcd.print("Z"); lcd.print(zoneNumber);
      //display start "mm/dd"
      lcd.print("  ");
      //handle next watering being scheduled for next month
      if(EEPROM.read(zoneNumber * 10 + sDay) < now.day()){
        if(now.month() + 1 == 13){month = 1;}else{month = now.month() + 1;}
        lcd.print(month);
        lcd.print("/");
        lcd.print(EEPROM.read(zoneNumber * 10 + sDay));
      }else{
        lcd.print(now.month());
        lcd.print("/");
        lcd.print(EEPROM.read(zoneNumber * 10 + sDay));
      }
      //display start "HH:MM"
      lcd.print("  ");
      updateDisplayedTime(EEPROM.read(zoneNumber * 10 + sHH), EEPROM.read(zoneNumber * 10 + sMM));
      //display duration
      lcd.setCursor(0,1);
      lcd.print("Dur: ");
      lcd.print(EEPROM.read(zoneNumber * 10 + sDur));
      //display frequency
      lcd.print("  Freq: ");
      lcd.print(EEPROM.read(zoneNumber * 10 + sFreq));
      lcd.setCursor(2,0);
      lcd.blink();
      updateDisplay = false;
    }
    //LEFT button action
    if(digitalRead(LEFT) == LOW){
      while(digitalRead(LEFT) == LOW){}
      if(zoneNumber == 1){zoneNumber = numberOfZones;}else{zoneNumber--;}
      time2 = millis();
      updateDisplay = true;      
    }
    //RIGHT button action
    if(digitalRead(RIGHT) == LOW){
      while(digitalRead(RIGHT) == LOW){}
      if(zoneNumber == numberOfZones){zoneNumber = 1;}else{zoneNumber++;}
      time2 = millis();
      updateDisplay = true;
      }
    //ENTER button action
     if(digitalRead(ENTER) == LOW){
      while(digitalRead(ENTER) == LOW){}
      lcd.noBlink();
      return;
      }
    time1 = millis();
    //check for zones to turn on or off
    activateScheduledZones();
    deactivateScheduledZones();

  }
  lcd.noBlink();
}

boolean selectStartTime(int *HH, int *MM){
  time1 = millis();  //used to time-out selectStartTime() after a few seconds
  time2 = millis();  //used to time-out selectStartTime() after a few seconds
  unsigned long timeA;
  unsigned long timeB;
  boolean updateDisplay = true;  //used to eliminate screen refresh flicker
  //selectStartTime() runs inside a loop that times out back to programZone() after 20 seconds.
  while(time1 < time2 + 20000){
    //clear display and display time selection promt.  only do this once.
    if(updateDisplay == true){
      lcd.clear();
      lcd.setCursor(2,0);
      lcd.print("Start Time?");
      lcd.setCursor(5,1);
      updateDisplayedTime(*HH,*MM);
      updateDisplay = false;
    }   
    //decreas time displayed by 1 minutes if LEFT button is pressed
    //fast decreas time if button is held
    if(digitalRead(LEFT) == 0){
      incrementTime(HH, MM, -1);
      lcd.setCursor(5,1);
      updateDisplayedTime(*HH,*MM);
      timeA = millis();
      timeB = millis();
      while(digitalRead(LEFT) == 0){
        if(timeA >= timeB + 800){  //if button held for 800 mSec or more, fast scroll
          incrementTime(HH, MM, -1);
          lcd.setCursor(5,1);
          updateDisplayedTime(*HH,*MM);
          delay(10); //prevents time from scrolling too fast
          }
        timeA = millis();
        }
      time2 = millis();  //advance time-out time after button press
    }
    //increase time displayed by 1 minutes if RIGHT button is pressed
    //fast decreas time if button is held
    if(digitalRead(RIGHT) == 0){
      incrementTime(HH, MM, 1);
      lcd.setCursor(5,1);
      updateDisplayedTime(*HH,*MM);
      timeA = millis();
      timeB = millis();
      while(digitalRead(RIGHT) == 0){
        if(timeA >= timeB + 800){  //if button held for 800 mSec or more, fast scroll
          incrementTime(HH, MM, 1);
          lcd.setCursor(5,1);
          updateDisplayedTime(*HH,*MM);
          delay(10); //prevents time from scrolling too fast
          }
        timeA = millis();
        }
      time2 = millis();  //advance time-out time after button press
    }
    //exit selectStartTime() if ENTER is pressed and released.
    if(digitalRead(ENTER) == 0){
      while(digitalRead(ENTER) == 0){}
      return(true);
    }
    time1 = millis();
    //check for zones to turn on or off
    activateScheduledZones();
    deactivateScheduledZones();
    }
    return(false); //return false if selectTime() times out.
}

boolean selectDuration(int *duration){
  time1 = millis();  //used to time-out selectDuration() after a few seconds
  time2 = millis();  //used to time-out selectDuration() after a few seconds
  int hh = 0;  
  boolean updateDisplay = true;  //used to eliminate screen refresh flicker
  //selectDuration() runs inside a loop that times out (returns false) after 20 seconds.
  while(time1 < time2 + 20000){
    //clear display and display 00:00 selection promt.  only do this once.
    if(updateDisplay == true){
      lcd.clear();
      lcd.setCursor(3,0);
      lcd.print("Duration?");
      lcd.setCursor(5,1);
      updateDisplayedTime(hh,*duration);
      updateDisplay = false;
    }

    //decreas time displayed by 1 minutes if LEFT button is pressed
    if(digitalRead(LEFT) == 0){
      while(digitalRead(LEFT) == 0){}
      incrementTime(0, duration, -1);
      lcd.setCursor(5,1);
      updateDisplayedTime(hh,*duration);
      time2 = millis();  //advance time-out time after button press
    }
    //increase time displayed by 1 minutes if RIGHT button is pressed
    if(digitalRead(RIGHT) == 0){
      while(digitalRead(RIGHT) == 0){}
      incrementTime(0, duration, 1);
      lcd.setCursor(5,1);
      updateDisplayedTime(hh,*duration);
      time2 = millis();  //advance time-out time after button press
    }

    //exit selectDuration() if ENTER is pressed and released.
    if(digitalRead(ENTER) == 0){
      while(digitalRead(ENTER) == 0){}
      return(true);
    }
    time1 = millis();
    //check for zones to turn on or off
    activateScheduledZones();
    deactivateScheduledZones();
    }
    return(false);
}

boolean selectFrequency(int *frequency){
  time1 = millis();  //used to time-out selectFrequency() after a few seconds
  time2 = millis();  //used to time-out selectFrequency() after a few seconds
  boolean updateDisplay = true;  //used to eliminate screen refresh flicker
  //selectFrequency() runs inside a loop that times out back to programZone() after 20 seconds.
  while(time1 < time2 + 20000){
    //clear display and display every "frequency" days promt.  only do this once.
    if(updateDisplay == true){
      lcd.clear();
      lcd.setCursor(3,0);
      lcd.print("Frequency?");
      lcd.setCursor(2,1);
      lcd.print("Every ");
      lcd.print(*frequency);
      lcd.print(" Days");
      updateDisplay = false;
    }
    //decreas time displayed by 1 days if LEFT button is pressed
    if(digitalRead(LEFT) == 0){
      while(digitalRead(LEFT) == 0){}
      if(*frequency > 1){*frequency = *frequency - 1;}else{*frequency = 1;}
      lcd.setCursor(8,1);
      lcd.print(*frequency);
      time2 = millis();  //advance time-out time after button press
    }
    //increase time displayed by 1 days if RIGHT button is pressed
    if(digitalRead(RIGHT) == 0){
      while(digitalRead(RIGHT) == 0){}
      if(*frequency < 7){*frequency = *frequency + 1;}else{*frequency = 7;}
      lcd.setCursor(8,1);
      lcd.print(*frequency);
      time2 = millis();  //advance time-out time after button press
    }
    //exit selectDuration() if ENTER is pressed and released.
    if(digitalRead(ENTER) == 0){
      while(digitalRead(ENTER) == 0){}
      return(true);
    }
    time1 = millis();
    //check for zones to turn on or off
    activateScheduledZones();
    deactivateScheduledZones();
  }
  return(false);  //return false if time-out
}

void updateDisplayedTime(int HH, int MM){
  //this fuction updates a HH:MM display on the LCD
  if(HH >= 10) lcd.print(HH);
  if(HH < 10){
    lcd.print("0");
    lcd.print(HH);
  }
  lcd.print(":");
  if(MM == 0) lcd.print("00");
  if(MM < 10 && MM != 0){
  lcd.print("0");
  lcd.print(MM);
  }
  if(MM >= 10) lcd.print(MM);
}

void incrementTime(int *HH, int *MM, int interval){  //"interval" is in minutes <60.
  //decrease time by "interval"
  if(interval < 0){
    if(*MM + interval >= 0){
      *MM = *MM + interval;
    }
    else if(*MM + interval < 0){
      *MM = 60 + *MM + interval;
      if(*HH == 1){*HH = 24;}else{*HH = *HH - 1;}
    }
  }
  //increase time by "interval"
  if(interval > 0){
    if(*MM + interval < 60){
      *MM = *MM + interval;
    }
    else if(*MM + interval >= 60){
      *MM = *MM + interval - 60;
      if(*HH == 24){*HH = 1;}else{*HH = *HH + 1;}
    }
  }
}

void updateDisplayedDate(){
  lcd.print(now.month()); lcd.print("/");
  lcd.print(now.day()); lcd.print("/");
  lcd.print(now.year()-2000);
}

boolean scheduleNextWatering(int zoneNumber){
  //use values stored in EEPROM for 'zoneNumber' to determine the next watering
  //date.  Store next watering date in EEPROM.  Return 'true' if completed
  //succesfully, return 'false' if something went amuck.

  //variables
  now = rtc.now();
  DateTime nextWaterDT;

  //determine if next watering is today or tomorrow
  //update EEPROM with todays date.day or tomorrows date.day
  //return true if write successfull.

  //TODO: should have used linux time to make this a lot easier...
  if(EEPROM.read(zoneNumber * 10 + sHH) > now.hour()){
    EEPROM.update(zoneNumber * 10 + sDay, now.day());
    EEPROM.update(zoneNumber * 10 + sMonth, now.month());
    EEPROM.update(zoneNumber * 10 + sYear, now.year() - 2000);
    if(EEPROM.read(zoneNumber * 10 + sDay) == now.day() &&
       EEPROM.read(zoneNumber * 10 + sMonth) == now.month() &&
       EEPROM.read(zoneNumber * 10 + sYear) + 2000 == now.year()
       ) return true;
  }
  if(EEPROM.read(zoneNumber * 10 + sHH) == now.hour()){
    if(EEPROM.read(zoneNumber * 10 + sMM) > now.minute()){
      EEPROM.update(zoneNumber * 10 + sDay, now.day());
      EEPROM.update(zoneNumber * 10 + sMonth, now.month());
      EEPROM.update(zoneNumber * 10 + sYear, now.year() - 2000);
      if(EEPROM.read(zoneNumber * 10 + sDay) == now.day() &&
         EEPROM.read(zoneNumber * 10 + sMonth) == now.month() &&
         EEPROM.read(zoneNumber * 10 + sYear) + 2000 == now.year()
      ) return true;
    }
    if(EEPROM.read(zoneNumber * 10 + sMM) <= now.minute()){
      nextWaterDT = now + TimeSpan(1,0,0,0);
      EEPROM.update(zoneNumber * 10 + sDay, nextWaterDT.day());
      EEPROM.update(zoneNumber * 10 + sMonth, nextWaterDT.month());
      EEPROM.update(zoneNumber * 10 + sYear, nextWaterDT.year() - 2000);
      if(EEPROM.read(zoneNumber * 10 + sDay) == nextWaterDT.day() &&
         EEPROM.read(zoneNumber * 10 + sMonth) == nextWaterDT.month() &&
         EEPROM.read(zoneNumber * 10 + sYear) + 2000 == nextWaterDT.year()
      ) return true;
    }
  }
  if(EEPROM.read(zoneNumber * 10 + sHH) < now.hour()){
    nextWaterDT = now + TimeSpan(1,0,0,0);
    EEPROM.update(zoneNumber * 10 + sDay, nextWaterDT.day());
    EEPROM.update(zoneNumber * 10 + sMonth, nextWaterDT.month());
    EEPROM.update(zoneNumber * 10 + sYear, nextWaterDT.year() - 2000);
    if(EEPROM.read(zoneNumber * 10 + sDay) == nextWaterDT.day() &&
       EEPROM.read(zoneNumber * 10 + sMonth) == nextWaterDT.month() &&
       EEPROM.read(zoneNumber * 10 + sYear) + 2000 == nextWaterDT.year()
    ) return true;
  }
 return false; 
}

void activateScheduledZones(){
  //call this function from all menu functions and the arduino will be able to 
  //turn zones on even when not on home screen.  cool.
  int zoneNumber;
  DateTime shutOffTime;
  DateTime nextWaterDay;

  for(zoneNumber = 1; zoneNumber <= numberOfZones; zoneNumber++){
    now = rtc.now();
    //check 'zoneNumber' schedule and activate if it's time.
    if(EEPROM.read(zoneNumber * 10 + sDay) == now.day() &&
       EEPROM.read(zoneNumber * 10 + sHH) == now.hour() &&
       EEPROM.read(zoneNumber * 10 + sMM) == now.minute()){
        //activate 'zoneNumber' 
        digitalWrite(zoneNumber + 7, LOW);  //zone relays on pins 8 through 12
        //write 'zoneNumber' shutoff HH and MM to EEPROM slots 5 and 6.
        shutOffTime = now + TimeSpan(0,0,EEPROM.read(zoneNumber * 10 + sDur),0);
        EEPROM.update(zoneNumber * 10 + sOffHH, shutOffTime.hour());
        EEPROM.update(zoneNumber * 10 + sOffMM, shutOffTime.minute());
        //determine next watering day and store in EEPROM
        nextWaterDay = now + TimeSpan(EEPROM.read(zoneNumber * 10 + sFreq),0,0,0);
        //schedule next watering, and prevent multiple calls to this function during sMM.
        EEPROM.update(zoneNumber * 10 + sDay, nextWaterDay.day());
        EEPROM.update(zoneNumber * 10 + sMonth, nextWaterDay.month());
        EEPROM.update(zoneNumber * 10 + sYear, nextWaterDay.year());
    }
  }

}

void deactivateScheduledZones(){
  //call this function from all menu functions and the arduino will be able to 
  //turn zones off even when not on home screen.  cool.
  int zoneNumber;

  for(zoneNumber = 1; zoneNumber <= numberOfZones; zoneNumber++){
    now = rtc.now();     
    //check 'zoneNumber' schedule and shutoff if it's time.
    if(digitalRead(zoneNumber + 7) == LOW &&
       EEPROM.read(zoneNumber * 10 + sOffHH) == now.hour() &&
       EEPROM.read(zoneNumber * 10 + sOffMM) == now.minute()){
      //shutoff 'zoneNumber' 
      digitalWrite(zoneNumber + 7, HIGH);  //zone relays on pins 8 through 12
    }
  }
}
