#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include "BanglaLabels.h"
#include "WeightBanglaLabels_persistent_final.h"
#include "ServoBanglaLabels.h"

#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4
#define TOUCH_CS 15
#define TOUCH_IRQ 27
#define RTC_SDA 21
#define RTC_SCL 22
#define BUZZER_PIN 26  ////25
#define HX711_DT 34
#define HX711_SCK 14
#define MP3_RX_PIN 33
#define MP3_TX_PIN 32

// ---------- servo pins ----------
#define SERVO_MORNING_PIN 13
//#define SERVO_NOON_PIN    17
#define SERVO_NOON_PIN    12
#define SERVO_NIGHT_PIN   25   ///26

#define TS_LEFT 3700
#define TS_RIGHT 600
#define TS_TOP 590
#define TS_BOTTOM 3500

Adafruit_ILI9341 tft(TFT_CS,TFT_DC,TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS,TOUCH_IRQ);
RTC_DS3231 rtc;
HardwareSerial mp3Serial(2);
DFRobotDFPlayerMini player;
HX711 scale;
Preferences weightPrefs;
Preferences medicinePrefs;

// Three medicine doors:
// Servo 0 = Morning, Servo 1 = Noon, Servo 2 = Night.
Servo servoMorning;
Servo servoNoon;
Servo servoNight;

Servo* doorServo[3] = {&servoMorning, &servoNoon, &servoNight};

// Calibrated positions from your servo test.
const int servoCloseAngle[3] = {70, 70, 165};//160
const int servoOpenAngle[3]  = {140, 150, 80};

// ---------- timing ----------
const unsigned long TEST_START=5000;
const unsigned long TEST_WAIT=40000;
const unsigned long REAL_WAIT=300000;        // 5 min scheduled response
const unsigned long MANUAL_TAKE_TIME=300000; // 5 min early/late take window
const unsigned long WEIGHT_UPDATE_MS=100;

// ---------- data ----------
struct Medicine{const char* name;int hour,minute;};
struct User{Medicine med[3];};
struct Button{int x,y,w,h;};

User users[3]={
 {{{"Napa 500",8,0},{"Seclo 20",14,0},{"Calbo-D",21,0}}},
 {{{"Ace Plus",7,30},{"Napa 500",13,30},{"Calbo-D",20,30}}},
 {{{"Seclo 20",9,0},{"Ace",15,0},{"Napa 500",22,0}}}
};

enum DoseStatus{READY,TAKEN,SKIPPED,MISSED,EARLY,LATE};
enum Screen{HOME,PROFILE,SCHEDULE,HISTORY,WEIGHT_MENU,WEIGHT_MEASURE,WEIGHT_HISTORY,WEIGHT_GRAPH,CLEAR_HISTORY_CONFIRM,TEST_MENU,TEST_COUNTDOWN,ALERT,CONFIRM_MANUAL,MANUAL_TAKE};
enum ActionMode{NORMAL_DOSE,EARLY_DOSE,LATE_DOSE};

Screen screen=HOME;
ActionMode actionMode=NORMAL_DOSE;

// ---------- buttons ----------
Button u1={10,92,90,75},u2={115,92,90,75},u3={220,92,90,75};
Button testBtn={90,184,140,44},backBtn={200,3,115,42};

// Profile buttons: 3 menu buttons + one full-width manual medicine button
Button scheduleBtn={10,158,92,34};
Button historyBtn={114,158,92,34};
Button weightBtn={218,158,92,34};
Button manualBtn={10,198,300,34};

// Weight screens
Button measureWeightBtn={20,72,280,55};
Button weightHistoryBtn={20,145,280,55};
Button zeroWeightBtn={10,185,90,42};
Button saveWeightBtn={115,185,90,42};
Button measureHistoryBtn={220,185,90,42};
Button graphBtn={10,207,140,30};
Button deleteWeightHistoryBtn={160,207,150,30};
Button deleteMedicineHistoryBtn={75,200,170,32};

Button morning={10,60,125,42},noonBtn={10,112,125,42},night={10,164,125,42};

// Direct servo test buttons.
Button testOpenBtn[3] = {
 {140,60,80,42},
 {140,112,80,42},
 {140,164,80,42}
};
Button testCloseBtn[3] = {
 {225,60,80,42},
 {225,112,80,42},
 {225,164,80,42}
};

Button takenBtn={25,180,125,45};
Button skipBtn={170,180,125,45};

Button yesBtn={35,170,110,45};
Button noBtn={175,170,110,45};

Button manualTakenBtn={25,180,125,45};
Button cancelBtn={170,180,125,45};

// ---------- state ----------
int selectedUser=-1,selectedShift=-1,activeUser=-1,activeMed=-1;
int realStatus[3][3]={{0}},testStatus[3]={0,0,0};
int actionMinutes[3][3]={{-1,-1,-1},{-1,-1,-1},{-1,-1,-1}};
bool medicineHistoryVisible[3][3]={{false}};
bool triggered[3][3]={{false}},isTest=false;

unsigned long testStartTime=0,reminderStart=0,manualTakeStart=0,lastClock=0;
long lastDisplayed=-999,lastManualDisplayed=-999;
int lastProfileMedicine=-99,lastProfileButtonState=-99;
long lastProfileSecond=-1;

int reminderNumber=1;
uint32_t runtimeDateCode=0;
Screen clearReturnScreen=HISTORY;

// ---------- audio mapping ----------
const int firstTrack[3]={1,4,7};
const int secondTrack[3]={2,5,8};
const int missedTrack[3]={3,6,9};
#define TAKEN_TRACK 10
#define SKIP_TRACK 11

// ---------- buzzer melody ----------
const int notes[]={660,784,660,784,660,784};
const int noteLen[]={700,700,700,700,700,1500};
int noteIndex=-1,pendingVoice=0;
unsigned long noteStarted=0;
bool melodyPlaying=false,waitingForVoice=false;
bool pendingServoOpen=false;

// The reminder door is opened only once, after the FIRST melody is fully finished.
// The second reminder melody never commands the servo.
bool reminderDoorOpen=false;

// Software buzzer: avoids LEDC/timer interaction between tone() and ESP32Servo PWM.
// This is important because the servo must stay still while the melody is playing.
bool buzzerRunning=false;
bool buzzerState=false;
unsigned long buzzerLastToggleUs=0;
unsigned long buzzerHalfPeriodUs=0;

// ---------- weight machine / persistent history ----------
const float calibrationFactor=24240.0;
const int MAX_WEIGHT_RECORDS=30;

struct WeightRecord{
 float weight;
 uint32_t timestamp;
};

WeightRecord weightRecords[3][MAX_WEIGHT_RECORDS];
uint8_t weightCount[3]={0,0,0};

const char* weightDataKey[3]={"u0data","u1data","u2data"};
const char* weightCountKey[3]={"u0cnt","u1cnt","u2cnt"};

float currentWeight=0.0;
bool currentWeightValid=false;
unsigned long lastWeightUpdate=0;
unsigned long weightMessageUntil=0;
String weightMessage="";
float lastDrawnWeight=99999.0;
bool hx711Ready=false;
uint8_t hx711FailCount=0;

// =====================================================
// helpers
// =====================================================
void bn(int x,int y,const uint8_t*b,int w,int h,uint16_t c){tft.drawBitmap(x,y,b,w,h,c);}
void bnC(const uint8_t*b,int w,int h,int x,int y,int bw,uint16_t c){bn(x+(bw-w)/2,y,b,w,h,c);}
bool inside(Button b,int x,int y){return x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;}

String timeText(int h,int m){
 bool pm=h>=12;int dh=h%12;if(!dh)dh=12;
 char b[16];sprintf(b,"%02d:%02d %s",dh,m,pm?"PM":"AM");return String(b);
}
String countText(long s){
 if(s<0)s=0;char b[10];sprintf(b,"%02ld:%02ld",s/60,s%60);return String(b);
}

void medBitmap(int u,int m,const uint8_t*&b,int&w,int&h){
 const char*n=users[u].med[m].name;
 if(!strcmp(n,"Napa 500")){b=BN_NAPA500;w=BN_NAPA500_W;h=BN_NAPA500_H;}
 else if(!strcmp(n,"Seclo 20")){b=BN_SECLO20;w=BN_SECLO20_W;h=BN_SECLO20_H;}
 else if(!strcmp(n,"Calbo-D")){b=BN_CALBOD;w=BN_CALBOD_W;h=BN_CALBOD_H;}
 else if(!strcmp(n,"Ace Plus")){b=BN_ACEPLUS;w=BN_ACEPLUS_W;h=BN_ACEPLUS_H;}
 else{b=BN_ACE;w=BN_ACE_W;h=BN_ACE_H;}
}

void shiftBitmap(int m,const uint8_t*&b,int&w,int&h){
 if(m==0){b=BN_MORNING;w=BN_MORNING_W;h=BN_MORNING_H;}
 else if(m==1){b=BN_AFTERNOON;w=BN_AFTERNOON_W;h=BN_AFTERNOON_H;}
 else{b=BN_NIGHT;w=BN_NIGHT_W;h=BN_NIGHT_H;}
}

void statusBitmap(int s,const uint8_t*&b,int&w,int&h,uint16_t&c){
 c=ILI9341_CYAN;
 if(s==TAKEN){b=BN_TAKEN;w=BN_TAKEN_W;h=BN_TAKEN_H;c=ILI9341_GREEN;}
 else if(s==SKIPPED){b=BN_SKIPPED;w=BN_SKIPPED_W;h=BN_SKIPPED_H;c=ILI9341_ORANGE;}
 else if(s==MISSED){b=BN_MISSED;w=BN_MISSED_W;h=BN_MISSED_H;c=ILI9341_RED;}
 else if(s==EARLY){b=BN_EARLY;w=BN_EARLY_W;h=BN_EARLY_H;c=ILI9341_GREEN;}
 else if(s==LATE){b=BN_LATE;w=BN_LATE_W;h=BN_LATE_H;c=ILI9341_YELLOW;}
 else{b=BN_READY;w=BN_READY_W;h=BN_READY_H;}
}

void drawBack(){
 tft.fillRoundRect(backBtn.x,backBtn.y,backBtn.w,backBtn.h,8,ILI9341_BLUE);
 tft.drawRoundRect(backBtn.x,backBtn.y,backBtn.w,backBtn.h,8,ILI9341_WHITE);
 bnC(BN_BACK,BN_BACK_W,BN_BACK_H,backBtn.x,backBtn.y+11,backBtn.w,ILI9341_WHITE);
}

void drawMenuBtn(Button b,const uint8_t*l,int w,int h,uint16_t c){
 tft.fillRoundRect(b.x,b.y,b.w,b.h,7,c);
 tft.drawRoundRect(b.x,b.y,b.w,b.h,7,ILI9341_WHITE);
 bnC(l,w,h,b.x,b.y+(b.h-h)/2,b.w,ILI9341_WHITE);
}

// =====================================================
// servo / medicine doors
// =====================================================

// Move one door smoothly using a for-loop.
// This uses your calibrated open/close angles.
void moveDoorSmooth(int door, int targetAngle){
  if(door<0 || door>2)return;

  int current=doorServo[door]->read();
  if(current<0)current=servoCloseAngle[door];

  if(current<targetAngle){
    for(int a=current;a<=targetAngle;a++){
      doorServo[door]->write(a);
      delay(10);
    }
  }else{
    for(int a=current;a>=targetAngle;a--){
      doorServo[door]->write(a);
      delay(10);
    }
  }

  doorServo[door]->write(targetAngle);
}

void openDoor(int door){
  moveDoorSmooth(door,servoOpenAngle[door]);
}

void closeDoor(int door){
  moveDoorSmooth(door,servoCloseAngle[door]);
}

void closeAllDoors(){
  // Startup safety: all medicine doors are closed.
  for(int i=0;i<3;i++){
    doorServo[i]->write(servoCloseAngle[i]);
  }
}

void setupServos(){
  servoMorning.setPeriodHertz(50);
  servoNoon.setPeriodHertz(50);
  servoNight.setPeriodHertz(50);

  servoMorning.attach(SERVO_MORNING_PIN,500,2400);
  servoNoon.attach(SERVO_NOON_PIN,500,2400);
  servoNight.attach(SERVO_NIGHT_PIN,500,2400);

  closeAllDoors();

  Serial.println("3 servo doors initialized and CLOSED.");
}

void openDoorForMedicine(int med){
  if(med<0 || med>2)return;
  Serial.print("Opening ");
  if(med==0)Serial.println("MORNING door");
  else if(med==1)Serial.println("NOON door");
  else Serial.println("NIGHT door");

  openDoor(med);
}

void closeDoorForMedicine(int med){
  if(med<0 || med>2)return;
  Serial.print("Closing ");
  if(med==0)Serial.println("MORNING door");
  else if(med==1)Serial.println("NOON door");
  else Serial.println("NIGHT door");

  closeDoor(med);
}

// =====================================================
// buzzer / voice
// =====================================================
void stopBuzzer(){
  digitalWrite(BUZZER_PIN,LOW);
  buzzerRunning=false;
  buzzerState=false;
}

void startBuzzerNote(int frequency){
  if(frequency<=0){
    stopBuzzer();
    return;
  }

  buzzerRunning=true;
  buzzerState=false;
  digitalWrite(BUZZER_PIN,LOW);
  buzzerHalfPeriodUs=500000UL/(unsigned long)frequency;
  buzzerLastToggleUs=micros();
}

// Generate the melody in software instead of tone().
// This keeps the buzzer completely separate from ESP32Servo's PWM hardware.
void updateBuzzer(){
  if(!buzzerRunning || buzzerHalfPeriodUs==0)return;

  unsigned long now=micros();
  if((unsigned long)(now-buzzerLastToggleUs)>=buzzerHalfPeriodUs){
    buzzerLastToggleUs=now;
    buzzerState=!buzzerState;
    digitalWrite(BUZZER_PIN,buzzerState?HIGH:LOW);
  }
}

void startAttentionVoice(int track,bool openDoorAfterBuzzer=false){
  player.stop();
  stopBuzzer();

  pendingVoice=track;
  pendingServoOpen=openDoorAfterBuzzer;

  // A new scheduled reminder starts with its door CLOSED.
  if(openDoorAfterBuzzer)
    reminderDoorOpen=false;

  noteIndex=0;
  melodyPlaying=true;
  noteStarted=millis();
  startBuzzerNote(notes[0]);
}

// Melody only: used after manual EARLY/LATE confirmation.
void startConfirmationMelody(){
  player.stop();
  stopBuzzer();
  pendingVoice=0;
  pendingServoOpen=false;

  noteIndex=0;
  melodyPlaying=true;
  noteStarted=millis();
  startBuzzerNote(notes[0]);
}

void updateMelody(){
  // Keep generating the current note without using tone().
  updateBuzzer();

  if(!melodyPlaying)return;
  if(millis()-noteStarted<noteLen[noteIndex])return;

  stopBuzzer();
  noteIndex++;

  if(noteIndex>=6){
    // IMPORTANT: the servo command happens only here, after ALL 6 notes.
    melodyPlaying=false;

    if(pendingServoOpen && !reminderDoorOpen && activeMed>=0 && activeMed<3){
      pendingServoOpen=false;

      // Small safety gap after the final buzzer note.
      delay(100);

      openDoorForMedicine(activeMed);
      reminderDoorOpen=true;
    }else{
      pendingServoOpen=false;
    }

    if(pendingVoice>0){
      delay(60);
      player.playMp3Folder(pendingVoice);
      pendingVoice=0;
    }

    // The response timer starts ONLY after the first melody has finished
    // and the door has been opened.
    if(screen==ALERT && waitingForVoice){
      reminderStart=millis();
      waitingForVoice=false;
      lastDisplayed=-999;
    }
    return;
  }

  noteStarted=millis();
  startBuzzerNote(notes[noteIndex]);
}

// =====================================================
// home
// =====================================================
void drawUserBtn(Button b,const uint8_t*l,int w,int h){
 tft.fillRoundRect(b.x,b.y,b.w,b.h,8,ILI9341_DARKCYAN);
 tft.drawRoundRect(b.x,b.y,b.w,b.h,8,ILI9341_CYAN);
 tft.fillCircle(b.x+b.w/2,b.y+22,12,ILI9341_CYAN);
 bnC(l,w,h,b.x,b.y+48,b.w,ILI9341_WHITE);
}

void updateClock(){
 DateTime n=rtc.now();
 tft.fillRect(190,3,125,38,ILI9341_NAVY);
 tft.drawRect(190,3,125,38,ILI9341_WHITE);
 tft.setTextSize(1);tft.setTextColor(ILI9341_YELLOW);
 tft.setCursor(215,9);tft.print(timeText(n.hour(),n.minute()));
 char d[15];sprintf(d,"%02d/%02d/%04d",n.day(),n.month(),n.year());
 tft.setTextColor(ILI9341_WHITE);tft.setCursor(215,25);tft.print(d);
}

void drawHome(){
 screen=HOME;selectedUser=-1;
 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,45,ILI9341_NAVY);

 bn(7,5,BN_SMART_MEDICINE,BN_SMART_MEDICINE_W,BN_SMART_MEDICINE_H,ILI9341_WHITE);
 bn(8,28,BN_REMINDER_SYSTEM,BN_REMINDER_SYSTEM_W,BN_REMINDER_SYSTEM_H,ILI9341_WHITE);
 bnC(BN_SELECT_USER,BN_SELECT_USER_W,BN_SELECT_USER_H,0,58,320,ILI9341_YELLOW);

 drawUserBtn(u1,BN_USER1,BN_USER1_W,BN_USER1_H);
 drawUserBtn(u2,BN_USER2,BN_USER2_W,BN_USER2_H);
 drawUserBtn(u3,BN_USER3,BN_USER3_W,BN_USER3_H);

 tft.fillRoundRect(testBtn.x,testBtn.y,testBtn.w,testBtn.h,8,ILI9341_ORANGE);
 tft.drawRoundRect(testBtn.x,testBtn.y,testBtn.w,testBtn.h,8,ILI9341_WHITE);
 bnC(BN_TEST_USER,BN_TEST_USER_W,BN_TEST_USER_H,testBtn.x,testBtn.y+13,testBtn.w,ILI9341_BLACK);

 updateClock();
}

// =====================================================
// real-user medicine logic
// =====================================================
int nextUpcoming(int u,DateTime n,long&remain){
 int cur=n.hour()*3600+n.minute()*60+n.second();
 for(int m=0;m<3;m++){
  if(realStatus[u][m]!=READY)continue;
  int t=users[u].med[m].hour*3600+users[u].med[m].minute*60;
  if(t>=cur){remain=t-cur;return m;}
 }
 remain=-1;return -1;
}

// Allow "take now" for overdue READY or scheduled MISSED medicine.
int overdueDose(int u,DateTime n){
 int cur=n.hour()*60+n.minute(),found=-1;
 for(int m=0;m<3;m++){
  int t=users[u].med[m].hour*60+users[u].med[m].minute;
  if(t<cur&&(realStatus[u][m]==READY||realStatus[u][m]==MISSED))found=m;
 }
 return found;
}

// =====================================================
// profile - flicker-free
// =====================================================
void drawTextBtn(Button b,const char*label,uint16_t c,uint8_t textSize){
 tft.fillRoundRect(b.x,b.y,b.w,b.h,7,c);
 tft.drawRoundRect(b.x,b.y,b.w,b.h,7,ILI9341_WHITE);
 tft.setTextSize(textSize);
 tft.setTextColor(ILI9341_WHITE);

 int16_t x1,y1;
 uint16_t tw,th;
 tft.getTextBounds(label,0,0,&x1,&y1,&tw,&th);
 tft.setCursor(b.x+(b.w-tw)/2,b.y+(b.h-th)/2);
 tft.print(label);
}

void updateProfile(){
 if(selectedUser<0)return;

 DateTime n=rtc.now();
 long remain;
 int m=nextUpcoming(selectedUser,n,remain);
 int overdue=overdueDose(selectedUser,n);

 // Redraw static medicine panel only when medicine changes.
 if(m!=lastProfileMedicine){
  lastProfileMedicine=m;
  lastProfileSecond=-1;

  tft.fillRect(12,60,296,90,ILI9341_BLACK);
  tft.drawRoundRect(10,58,300,94,8,ILI9341_CYAN);

  if(m>=0){
   bn(20,64,BN_NEXT_MED,BN_NEXT_MED_W,BN_NEXT_MED_H,ILI9341_CYAN);

   const uint8_t*b;int w,h;
   medBitmap(selectedUser,m,b,w,h);
   bnC(b,w,h,10,82,300,ILI9341_WHITE);

   bn(22,108,BN_SCHEDULED,BN_SCHEDULED_W,BN_SCHEDULED_H,ILI9341_YELLOW);
   tft.setTextSize(1);tft.setTextColor(ILI9341_YELLOW);
   tft.setCursor(132,108);
   tft.print(timeText(users[selectedUser].med[m].hour,users[selectedUser].med[m].minute));

   bn(22,130,BN_REMAINING,BN_REMAINING_W,BN_REMAINING_H,ILI9341_CYAN);
  }else{
   wbnC(WBN_NO_MORE_MED_TODAY,WBN_NO_MORE_MED_TODAY_W,WBN_NO_MORE_MED_TODAY_H,10,96,300,ILI9341_WHITE);
  }
 }

 // Update only countdown digits once per second.
 if(m>=0&&remain!=lastProfileSecond){
  lastProfileSecond=remain;

  char x[15];
  sprintf(x,"%02ld:%02ld:%02ld",remain/3600,(remain%3600)/60,remain%60);

  tft.fillRect(115,126,120,20,ILI9341_BLACK);
  tft.setTextSize(1);tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(120,130);tft.print(x);
 }

 // Update manual action button only when its type changes.
 int buttonState=(overdue>=0)?2:(m>=0?1:0);
 if(buttonState!=lastProfileButtonState){
  lastProfileButtonState=buttonState;
  tft.fillRect(manualBtn.x,manualBtn.y,manualBtn.w,manualBtn.h,ILI9341_BLACK);

  if(buttonState==2)
   drawMenuBtn(manualBtn,BN_TAKE_NOW,BN_TAKE_NOW_W,BN_TAKE_NOW_H,ILI9341_RED);
  else if(buttonState==1)
   drawMenuBtn(manualBtn,BN_TAKE_EARLY,BN_TAKE_EARLY_W,BN_TAKE_EARLY_H,ILI9341_DARKCYAN);
 }
}

void drawProfile(int u){
 selectedUser=u;screen=PROFILE;

 lastProfileMedicine=-99;
 lastProfileSecond=-1;
 lastProfileButtonState=-99;

 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_NAVY);

 const uint8_t*b=u==0?BN_USER1:u==1?BN_USER2:BN_USER3;
 int w=u==0?BN_USER1_W:u==1?BN_USER2_W:BN_USER3_W;
 int h=u==0?BN_USER1_H:u==1?BN_USER2_H:BN_USER3_H;

 bn(8,14,b,w,h,ILI9341_WHITE);
 drawBack();

 drawMenuBtn(scheduleBtn,BN_SCHEDULE,BN_SCHEDULE_W,BN_SCHEDULE_H,ILI9341_BLUE);
 drawMenuBtn(historyBtn,BN_HISTORY,BN_HISTORY_W,BN_HISTORY_H,ILI9341_BLUE);
 drawWeightProfileButton();

 updateProfile();
}

// =====================================================
// schedule / history
// =====================================================
void drawSchedule(){
 screen=SCHEDULE;
 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_NAVY);

 bn(8,14,BN_TODAY_SCHEDULE,BN_TODAY_SCHEDULE_W,BN_TODAY_SCHEDULE_H,ILI9341_WHITE);
 drawBack();

 for(int m=0;m<3;m++){
  int y=60+m*56;
  const uint8_t*sb,*mb,*st;
  int sw,sh,mw,mh,stw,sth;
  uint16_t c;

  shiftBitmap(m,sb,sw,sh);
  medBitmap(selectedUser,m,mb,mw,mh);
  statusBitmap(realStatus[selectedUser][m],st,stw,sth,c);

  tft.drawRoundRect(10,y,300,48,7,c);
  bn(18,y+4,sb,sw,sh,ILI9341_CYAN);
  bn(78,y+5,mb,mw,mh,ILI9341_WHITE);

  tft.setTextSize(1);tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(220,y+7);
  tft.print(timeText(users[selectedUser].med[m].hour,users[selectedUser].med[m].minute));

  bn(220,y+25,st,stw,sth,c);
 }
}

void drawHistory(){
 screen=HISTORY;
 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_NAVY);

 bn(8,14,BN_TODAY_HISTORY,BN_TODAY_HISTORY_W,BN_TODAY_HISTORY_H,ILI9341_WHITE);
 drawBack();

 bool any=false;
 int row=0;

 for(int m=0;m<3;m++){
  if(realStatus[selectedUser][m]==READY)continue;
  if(!medicineHistoryVisible[selectedUser][m])continue;

  any=true;
  int y=58+row*44;
  row++;

  const uint8_t*mb,*st;
  int mw,mh,stw,sth;
  uint16_t c;

  medBitmap(selectedUser,m,mb,mw,mh);
  statusBitmap(realStatus[selectedUser][m],st,stw,sth,c);

  tft.drawRoundRect(10,y,300,38,7,c);
  bn(18,y+4,mb,mw,mh,ILI9341_WHITE);
  bn(155,y+4,st,stw,sth,c);

  tft.setTextSize(1);tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(238,y+7);

  if(actionMinutes[selectedUser][m]>=0)
   tft.print(timeText(actionMinutes[selectedUser][m]/60,actionMinutes[selectedUser][m]%60));
  else
   tft.print("--:--");
 }

 if(!any)
  bnC(BN_NO_HISTORY,BN_NO_HISTORY_W,BN_NO_HISTORY_H,0,112,320,ILI9341_WHITE);

 // This clears BOTH saved medicine-history visibility and all saved weight records.
 drawBanglaBtn(
  deleteMedicineHistoryBtn,
  WBN_DELETE_ALL_HISTORY,WBN_DELETE_ALL_HISTORY_W,WBN_DELETE_ALL_HISTORY_H,
  ILI9341_RED
 );
}

// =====================================================
// persistent medicine history/state
// =====================================================
const uint32_t MED_STORE_MAGIC=0x4D484953UL; // "MHIS"

struct MedicineStore{
 uint32_t magic;
 uint16_t year;
 uint8_t month;
 uint8_t day;
 uint8_t status[3][3];
 int16_t actionMin[3][3];
 uint8_t visible[3][3];
};

uint32_t makeDateCode(const DateTime &n){
 return (uint32_t)n.year()*10000UL+(uint32_t)n.month()*100UL+n.day();
}

void resetMedicineRam(){
 for(int u=0;u<3;u++){
  for(int m=0;m<3;m++){
   realStatus[u][m]=READY;
   actionMinutes[u][m]=-1;
   medicineHistoryVisible[u][m]=false;
   triggered[u][m]=false;
  }
 }
}

void persistMedicineState(){
 DateTime n=rtc.now();
 MedicineStore s={};

 s.magic=MED_STORE_MAGIC;
 s.year=n.year();
 s.month=n.month();
 s.day=n.day();

 for(int u=0;u<3;u++){
  for(int m=0;m<3;m++){
   s.status[u][m]=(uint8_t)realStatus[u][m];
   s.actionMin[u][m]=(int16_t)actionMinutes[u][m];
   s.visible[u][m]=medicineHistoryVisible[u][m]?1:0;
  }
 }

 medicinePrefs.putBytes("todaystate",&s,sizeof(s));
}

void loadMedicineState(){
 medicinePrefs.begin("medhist",false);
 resetMedicineRam();

 DateTime now=rtc.now();
 runtimeDateCode=makeDateCode(now);

 MedicineStore s={};
 size_t got=medicinePrefs.getBytes("todaystate",&s,sizeof(s));

 bool valid=(got==sizeof(s) &&
             s.magic==MED_STORE_MAGIC &&
             s.year==now.year() &&
             s.month==now.month() &&
             s.day==now.day());

 if(!valid){
  // Old/no data or a new RTC date: start today's medicine state clean.
  medicinePrefs.clear();
  persistMedicineState();
  return;
 }

 for(int u=0;u<3;u++){
  for(int m=0;m<3;m++){
   uint8_t st=s.status[u][m];
   if(st>LATE)st=READY;

   realStatus[u][m]=(int)st;

   int mins=s.actionMin[u][m];
   if(mins<-1 || mins>1439)mins=-1;
   actionMinutes[u][m]=mins;

   medicineHistoryVisible[u][m]=(s.visible[u][m]!=0 && st!=READY);

   // Prevent already-completed doses from alarming again after a power cycle.
   triggered[u][m]=(st!=READY);
  }
 }
}

// =====================================================
// weight machine / storage
// =====================================================
void loadWeightHistory(){
 weightPrefs.begin("weights",false);

 for(int u=0;u<3;u++){
  weightCount[u]=weightPrefs.getUChar(weightCountKey[u],0);

  if(weightCount[u]>MAX_WEIGHT_RECORDS)
   weightCount[u]=0;

  size_t got=weightPrefs.getBytes(
   weightDataKey[u],
   weightRecords[u],
   sizeof(weightRecords[u])
  );

  if(got!=sizeof(weightRecords[u])){
   memset(weightRecords[u],0,sizeof(weightRecords[u]));
   weightCount[u]=0;
  }
 }
}

void persistWeightUser(int u){
 weightPrefs.putUChar(weightCountKey[u],weightCount[u]);
 weightPrefs.putBytes(
  weightDataKey[u],
  weightRecords[u],
  sizeof(weightRecords[u])
 );
}

void addWeightRecord(int u,float w){
 if(u<0||u>2)return;

 if(weightCount[u]<MAX_WEIGHT_RECORDS){
  int i=weightCount[u];
  weightRecords[u][i].weight=w;
  weightRecords[u][i].timestamp=rtc.now().unixtime();
  weightCount[u]++;
 }else{
  for(int i=1;i<MAX_WEIGHT_RECORDS;i++)
   weightRecords[u][i-1]=weightRecords[u][i];

  weightRecords[u][MAX_WEIGHT_RECORDS-1].weight=w;
  weightRecords[u][MAX_WEIGHT_RECORDS-1].timestamp=rtc.now().unixtime();
 }

 persistWeightUser(u);
}

// Delete saved history without changing today's operational medicine status.
// This is important: a dose that was already taken stays TAKEN internally,
// so deleting history cannot accidentally trigger the same medicine again.
void clearAllSavedHistory(){
 // Clear weight history for all three users.
 for(int u=0;u<3;u++){
  weightCount[u]=0;
  memset(weightRecords[u],0,sizeof(weightRecords[u]));
 }
 weightPrefs.clear();

 // Hide/erase medicine history records, but preserve realStatus/actionMinutes
 // so the reminder logic remains safe for the rest of the day.
 for(int u=0;u<3;u++){
  for(int m=0;m<3;m++){
   medicineHistoryVisible[u][m]=false;
  }
 }

 medicinePrefs.clear();
 persistMedicineState();
}

void returnFromClearHistory(){
 if(clearReturnScreen==WEIGHT_HISTORY)
  drawWeightHistory();
 else
  drawHistory();
}

void drawClearHistoryConfirm(Screen returnTo){
 clearReturnScreen=returnTo;
 screen=CLEAR_HISTORY_CONFIRM;

 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_RED);
 wbnC(
  WBN_DELETE_ALL_HISTORY,WBN_DELETE_ALL_HISTORY_W,WBN_DELETE_ALL_HISTORY_H,
  0,14,320,ILI9341_WHITE
 );

 tft.drawRoundRect(12,72,296,72,8,ILI9341_ORANGE);
 wbnC(
  WBN_DELETE_CONFIRM,WBN_DELETE_CONFIRM_W,WBN_DELETE_CONFIRM_H,
  12,97,296,ILI9341_YELLOW
 );

 drawMenuBtn(yesBtn,BN_YES,BN_YES_W,BN_YES_H,ILI9341_RED);
 drawMenuBtn(noBtn,BN_NO,BN_NO_W,BN_NO_H,ILI9341_BLUE);
}

void executeClearAllHistory(){
 clearAllSavedHistory();

 tft.fillScreen(ILI9341_BLACK);
 wbnC(
  WBN_HISTORY_CLEARED,WBN_HISTORY_CLEARED_W,WBN_HISTORY_CLEARED_H,
  0,108,320,ILI9341_GREEN
 );
 delay(900);

 returnFromClearHistory();
}

// 24-hour numeric time avoids English AM/PM on the Bangla history page.
String shortDateTime(uint32_t ts){
 DateTime d(ts);
 char b[22];
 sprintf(
  b,"%02d/%02d/%02d  %02d:%02d",
  d.day(),d.month(),d.year()%100,d.hour(),d.minute()
 );
 return String(b);
}

// ---------- Bangla bitmap helpers for weight pages ----------
void wbn(int x,int y,const uint8_t*b,int w,int h,uint16_t c){
 tft.drawBitmap(x,y,b,w,h,c);
}

void wbnC(const uint8_t*b,int w,int h,int x,int y,int bw,uint16_t c){
 wbn(x+(bw-w)/2,y,b,w,h,c);
}

void drawBanglaBtn(Button b,const uint8_t*label,int w,int h,uint16_t c){
 tft.fillRoundRect(b.x,b.y,b.w,b.h,7,c);
 tft.drawRoundRect(b.x,b.y,b.w,b.h,7,ILI9341_WHITE);
 wbnC(label,w,h,b.x,b.y+(b.h-h)/2,b.w,ILI9341_WHITE);
}

void drawWeightHeader(const uint8_t*title,int tw,int th){
 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_DARKCYAN);

 // Page title
 wbn(8,5,title,tw,th,ILI9341_WHITE);

 // Current user
 const uint8_t*ub=selectedUser==0?BN_USER1:selectedUser==1?BN_USER2:BN_USER3;
 int uw=selectedUser==0?BN_USER1_W:selectedUser==1?BN_USER2_W:BN_USER3_W;
 int uh=selectedUser==0?BN_USER1_H:selectedUser==1?BN_USER2_H:BN_USER3_H;
 bn(8,28,ub,uw,uh,ILI9341_YELLOW);

 drawBack();
}

void drawWeightProfileButton(){
 drawBanglaBtn(weightBtn,WBN_WEIGHT,WBN_WEIGHT_W,WBN_WEIGHT_H,ILI9341_BLUE);
}

void drawWeightMenu(){
 screen=WEIGHT_MENU;
 drawWeightHeader(WBN_WEIGHT,WBN_WEIGHT_W,WBN_WEIGHT_H);

 drawBanglaBtn(
  measureWeightBtn,
  WBN_MEASURE_WEIGHT,WBN_MEASURE_WEIGHT_W,WBN_MEASURE_WEIGHT_H,
  ILI9341_DARKCYAN
 );

 drawBanglaBtn(
  weightHistoryBtn,
  WBN_WEIGHT_HISTORY,WBN_WEIGHT_HISTORY_W,WBN_WEIGHT_HISTORY_H,
  ILI9341_BLUE
 );
}

void drawLastSavedWeight(){
 tft.fillRect(35,145,250,25,ILI9341_BLACK);

 wbn(45,149,WBN_LAST_SAVED,WBN_LAST_SAVED_W,WBN_LAST_SAVED_H,ILI9341_WHITE);

 if(weightCount[selectedUser]>0){
  int i=weightCount[selectedUser]-1;

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREEN,ILI9341_BLACK);
  tft.setCursor(135,151);
  tft.print(weightRecords[selectedUser][i].weight,2);

  wbn(180,149,WBN_KG,WBN_KG_W,WBN_KG_H,ILI9341_GREEN);
 }else{
  tft.setTextColor(ILI9341_YELLOW);
  wbn(135,149,WBN_NO_SAVED,WBN_NO_SAVED_W,WBN_NO_SAVED_H,ILI9341_YELLOW);
 }
}

void drawWeightMeasure(){
 screen=WEIGHT_MEASURE;
 currentWeightValid=false;
 lastWeightUpdate=0;
 lastDrawnWeight=99999.0;
 weightMessage="";
 weightMessageUntil=0;
 hx711FailCount=0;

 drawWeightHeader(WBN_MEASURE_WEIGHT,WBN_MEASURE_WEIGHT_W,WBN_MEASURE_WEIGHT_H);

 wbnC(
  WBN_LIVE_WEIGHT,WBN_LIVE_WEIGHT_W,WBN_LIVE_WEIGHT_H,
  0,55,320,ILI9341_CYAN
 );

 tft.drawRoundRect(20,72,280,68,8,ILI9341_CYAN);

 // This numeric field is updated without clearing the whole box.
 tft.setTextSize(4);
 tft.setTextColor(ILI9341_WHITE,ILI9341_BLACK);
 tft.setCursor(54,91);
 tft.print("  --.--");
 wbn(225,98,WBN_KG,WBN_KG_W,WBN_KG_H,ILI9341_WHITE);

 drawLastSavedWeight();

 drawBanglaBtn(
  zeroWeightBtn,
  WBN_ZERO,WBN_ZERO_W,WBN_ZERO_H,
  ILI9341_ORANGE
 );

 drawBanglaBtn(
  saveWeightBtn,
  WBN_SAVE,WBN_SAVE_W,WBN_SAVE_H,
  ILI9341_GREEN
 );

 drawBanglaBtn(
  measureHistoryBtn,
  WBN_HISTORY,WBN_HISTORY_W,WBN_HISTORY_H,
  ILI9341_BLUE
 );
}

void clearWeightMessage(){
 tft.fillRect(10,166,300,16,ILI9341_BLACK);
 weightMessageUntil=0;
 weightMessage="";
}

void showWeightMessageBn(
 const uint8_t*b,int w,int h,uint16_t c,
 unsigned long duration=1600
){
 tft.fillRect(10,164,300,18,ILI9341_BLACK);
 wbnC(b,w,h,10,165,300,c);
 weightMessageUntil=millis()+duration;
}

// Draw only the numeric characters.  No fillRect here, so the live value
// does not flash every time a new HX711 sample arrives.
void drawLiveWeightNumber(float w){
 char valueText[12];
 snprintf(valueText,sizeof(valueText),"%7.2f",w);

 tft.setTextSize(4);
 tft.setTextColor(ILI9341_GREEN,ILI9341_BLACK);
 tft.setCursor(54,91);
 tft.print(valueText);

 // "কেজি" is static, but redraw it in case this is the first valid reading.
 wbn(225,98,WBN_KG,WBN_KG_W,WBN_KG_H,ILI9341_GREEN);
}

void showScaleNotReady(){
 currentWeightValid=false;
 showWeightMessageBn(
  WBN_SCALE_NOT_READY,
  WBN_SCALE_NOT_READY_W,WBN_SCALE_NOT_READY_H,
  ILI9341_RED,1800
 );
}

void updateWeightMeasure(){
 if(screen!=WEIGHT_MEASURE)return;

 if(weightMessageUntil>0 && (long)(millis()-weightMessageUntil)>=0)
  clearWeightMessage();

 if(millis()-lastWeightUpdate<WEIGHT_UPDATE_MS)return;
 lastWeightUpdate=millis();

 // IMPORTANT:
 // is_ready() checks only one instant.  The HX711 DOUT line is HIGH while
 // it is preparing its next conversion, which caused the old code to
 // alternate between "NOT READY" and a weight value.
 // Wait briefly for the next conversion instead.
 if(!scale.wait_ready_timeout(500)){
  if(hx711FailCount<255)hx711FailCount++;

  // Ignore one or two temporary misses.  Only show an error after
  // repeated failures, and do not erase the last good weight.
  if(hx711FailCount==3)
   showScaleNotReady();

  return;
 }

 hx711FailCount=0;
 hx711Ready=true;

 // Keep the exact averaging value that worked in your standalone test.
 float w=scale.get_units(5);

 // Confirmed body-scale zero-drift filter.
 if(w>-2.0 && w<2.0)
  w=0.0;

 currentWeight=w;
 currentWeightValid=true;

 float drawDiff=currentWeight-lastDrawnWeight;
 if(drawDiff<0)drawDiff=-drawDiff;

 // 0.05 kg display threshold keeps the display stable and reduces redraws.
 if(lastDrawnWeight!=99999.0 && drawDiff<0.05)
  return;

 lastDrawnWeight=currentWeight;
 drawLiveWeightNumber(currentWeight);
}

void tareWeightScale(){
 showWeightMessageBn(
  WBN_ZEROING,WBN_ZEROING_W,WBN_ZEROING_H,
  ILI9341_YELLOW,6000
 );

 // A timeout-based readiness check is much safer than is_ready().
 if(!scale.wait_ready_timeout(2000)){
  hx711Ready=false;
  showScaleNotReady();
  return;
 }

 // Keep the platform empty while this runs.
 scale.tare(50);

 hx711Ready=true;
 hx711FailCount=0;
 currentWeight=0.0;
 currentWeightValid=true;
 lastDrawnWeight=99999.0;
 lastWeightUpdate=0;

 drawLiveWeightNumber(0.0);

 showWeightMessageBn(
  WBN_ZERO_COMPLETE,WBN_ZERO_COMPLETE_W,WBN_ZERO_COMPLETE_H,
  ILI9341_GREEN
 );
}

void saveCurrentWeight(){
 if(!currentWeightValid){
  showWeightMessageBn(
   WBN_NO_VALID_WEIGHT,
   WBN_NO_VALID_WEIGHT_W,WBN_NO_VALID_WEIGHT_H,
   ILI9341_RED
  );
  return;
 }

 if(currentWeight<=2.0){
  showWeightMessageBn(
   WBN_STAND_FIRST,
   WBN_STAND_FIRST_W,WBN_STAND_FIRST_H,
   ILI9341_ORANGE
  );
  return;
 }

 addWeightRecord(selectedUser,currentWeight);

 showWeightMessageBn(
  WBN_WEIGHT_SAVED,
  WBN_WEIGHT_SAVED_W,WBN_WEIGHT_SAVED_H,
  ILI9341_GREEN
 );

 drawLastSavedWeight();
}

void drawWeightHistory(){
 screen=WEIGHT_HISTORY;
 drawWeightHeader(
  WBN_WEIGHT_HISTORY,WBN_WEIGHT_HISTORY_W,WBN_WEIGHT_HISTORY_H
 );

 if(weightCount[selectedUser]==0){
  wbnC(
   WBN_NO_SAVED,WBN_NO_SAVED_W,WBN_NO_SAVED_H,
   0,105,320,ILI9341_WHITE
  );
 }else{
  tft.setTextSize(1);

  int shown=weightCount[selectedUser];
  if(shown>5)shown=5;

  for(int row=0;row<shown;row++){
  int i=weightCount[selectedUser]-1-row;
  int y=58+row*27;

  tft.drawRoundRect(8,y,304,22,5,ILI9341_CYAN);

  // Weight number
  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(15,y+7);
  tft.print(weightRecords[selectedUser][i].weight,2);

  // Bangla "কেজি"
  wbn(49,y+4,WBN_KG,WBN_KG_W,WBN_KG_H,ILI9341_GREEN);

  // Date + time, 24-hour numeric
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(95,y+7);
  tft.print(shortDateTime(weightRecords[selectedUser][i].timestamp));
 }

  if(weightCount[selectedUser]>5){
   wbn(10,190,WBN_LATEST,WBN_LATEST_W,WBN_LATEST_H,ILI9341_YELLOW);

   tft.setTextColor(ILI9341_YELLOW);
   tft.setTextSize(1);
   tft.setCursor(58,194);
   tft.print("5/");

   tft.print(weightCount[selectedUser]);

   wbn(90,190,WBN_MEASUREMENTS,WBN_MEASUREMENTS_W,WBN_MEASUREMENTS_H,ILI9341_YELLOW);
  }
 }

 drawBanglaBtn(
  graphBtn,WBN_GRAPH,WBN_GRAPH_W,WBN_GRAPH_H,ILI9341_BLUE
 );
 drawBanglaBtn(
  deleteWeightHistoryBtn,
  WBN_DELETE_ALL_HISTORY,WBN_DELETE_ALL_HISTORY_W,WBN_DELETE_ALL_HISTORY_H,
  ILI9341_RED
 );
}

void drawWeightGraph(){
 screen=WEIGHT_GRAPH;
 drawWeightHeader(
  WBN_WEIGHT_GRAPH,WBN_WEIGHT_GRAPH_W,WBN_WEIGHT_GRAPH_H
 );

 int count=weightCount[selectedUser];

 if(count==0){
  wbnC(
   WBN_NO_SAVED,WBN_NO_SAVED_W,WBN_NO_SAVED_H,
   0,110,320,ILI9341_WHITE
  );
  return;
 }

 int n=count;
 if(n>10)n=10;
 int start=count-n;

 float minW=weightRecords[selectedUser][start].weight;
 float maxW=minW;

 for(int i=start;i<count;i++){
  if(weightRecords[selectedUser][i].weight<minW)
   minW=weightRecords[selectedUser][i].weight;

  if(weightRecords[selectedUser][i].weight>maxW)
   maxW=weightRecords[selectedUser][i].weight;
 }

 if(maxW-minW<4.0){
  float mid=(maxW+minW)/2.0;
  minW=mid-2.0;
  maxW=mid+2.0;
 }else{
  minW-=1.0;
  maxW+=1.0;
 }

 const int gx=40;
 const int gy=58;
 const int gw=260;
 const int gh=125;

 tft.drawLine(gx,gy,gx,gy+gh,ILI9341_WHITE);
 tft.drawLine(gx,gy+gh,gx+gw,gy+gh,ILI9341_WHITE);

 tft.setTextSize(1);
 tft.setTextColor(ILI9341_YELLOW);
 tft.setCursor(3,gy);
 tft.print(maxW,1);

 tft.setCursor(3,gy+gh-6);
 tft.print(minW,1);

 int prevX=-1,prevY=-1;

 for(int j=0;j<n;j++){
  int i=start+j;

  int x;
  if(n==1)x=gx+gw/2;
  else x=gx+(long)j*gw/(n-1);

  float ratio=(weightRecords[selectedUser][i].weight-minW)/(maxW-minW);
  int y=gy+gh-(int)(ratio*gh);

  if(prevX>=0)
   tft.drawLine(prevX,prevY,x,y,ILI9341_CYAN);

  tft.fillCircle(x,y,3,ILI9341_GREEN);

  prevX=x;
  prevY=y;
 }

 DateTime firstD(weightRecords[selectedUser][start].timestamp);
 DateTime lastD(weightRecords[selectedUser][count-1].timestamp);

 char d1[8],d2[8];
 sprintf(d1,"%02d/%02d",firstD.day(),firstD.month());
 sprintf(d2,"%02d/%02d",lastD.day(),lastD.month());

 tft.setTextColor(ILI9341_WHITE);
 tft.setCursor(gx,190);
 tft.print(d1);

 int16_t x1,y1;
 uint16_t tw,th;
 tft.getTextBounds(d2,0,0,&x1,&y1,&tw,&th);
 tft.setCursor(gx+gw-tw,190);
 tft.print(d2);

 // "সর্বশেষ N টি মাপ"
 wbn(90,210,WBN_LATEST,WBN_LATEST_W,WBN_LATEST_H,ILI9341_CYAN);
 tft.setTextSize(1);
 tft.setTextColor(ILI9341_CYAN);
 tft.setCursor(140,214);
 tft.print(n);
 wbn(157,210,WBN_MEASUREMENTS,WBN_MEASUREMENTS_W,WBN_MEASUREMENTS_H,ILI9341_CYAN);
}

// =====================================================
// early/late confirmation screen
// =====================================================
void drawConfirmManual(int m,ActionMode mode){
 activeUser=selectedUser;
 activeMed=m;
 actionMode=mode;
 isTest=false;
 screen=CONFIRM_MANUAL;

 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,mode==EARLY_DOSE?ILI9341_DARKCYAN:ILI9341_RED);
 drawBack();

 const uint8_t*mb;int mw,mh;
 medBitmap(activeUser,activeMed,mb,mw,mh);
 bnC(mb,mw,mh,0,78,320,ILI9341_WHITE);

 if(mode==EARLY_DOSE)
  bnC(BN_CONFIRM_EARLY,BN_CONFIRM_EARLY_W,BN_CONFIRM_EARLY_H,0,125,320,ILI9341_YELLOW);
 else
  bnC(BN_CONFIRM_NOW,BN_CONFIRM_NOW_W,BN_CONFIRM_NOW_H,0,125,320,ILI9341_YELLOW);

 drawMenuBtn(yesBtn,BN_YES,BN_YES_W,BN_YES_H,ILI9341_GREEN);
 drawMenuBtn(noBtn,BN_NO,BN_NO_W,BN_NO_H,ILI9341_RED);
}

// =====================================================
// manual early/late 5-minute take window
// NO VOICE, NO BUZZER while waiting.
// =====================================================
void startManualTakeWindow(){
 // Manual early/late taking also needs the correct compartment open.
 openDoorForMedicine(activeMed);

 screen=MANUAL_TAKE;
 manualTakeStart=millis();
 lastManualDisplayed=-999;

 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,actionMode==EARLY_DOSE?ILI9341_DARKCYAN:ILI9341_RED);
 drawBack();

 const uint8_t*mb;int mw,mh;
 medBitmap(activeUser,activeMed,mb,mw,mh);
 bnC(mb,mw,mh,0,70,320,ILI9341_WHITE);

 bnC(BN_TAKE_MED_NOW,BN_TAKE_MED_NOW_W,BN_TAKE_MED_NOW_H,0,112,320,ILI9341_YELLOW);
 bn(40,145,BN_REMAINING,BN_REMAINING_W,BN_REMAINING_H,ILI9341_CYAN);

 drawMenuBtn(manualTakenBtn,BN_TAKEN,BN_TAKEN_W,BN_TAKEN_H,ILI9341_GREEN);
 drawMenuBtn(cancelBtn,BN_CANCEL,BN_CANCEL_W,BN_CANCEL_H,ILI9341_BLUE);
}

void updateManualTake(){
 unsigned long e=millis()-manualTakeStart;
 long r=e>=MANUAL_TAKE_TIME?0:(MANUAL_TAKE_TIME-e+999)/1000;

 if(r!=lastManualDisplayed){
  lastManualDisplayed=r;

  tft.fillRect(155,140,110,28,ILI9341_BLACK);
  tft.setTextSize(2);tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(170,144);
  tft.print(countText(r));
 }

 // Timeout: no status, no history, no sound.
 if(e>=MANUAL_TAKE_TIME){
  int u=activeUser;
  drawProfile(u);
 }
}

void confirmManualTaken(){
 DateTime n=rtc.now();

 closeDoorForMedicine(activeMed);
 reminderDoorOpen=false;

 realStatus[activeUser][activeMed]=(actionMode==EARLY_DOSE)?EARLY:LATE;
 actionMinutes[activeUser][activeMed]=n.hour()*60+n.minute();
 medicineHistoryVisible[activeUser][activeMed]=true;
 triggered[activeUser][activeMed]=true;
 persistMedicineState();

 // Confirmation buzzer only. NO voice for early/late.
 startConfirmationMelody();

 int u=activeUser;
 drawProfile(u);
}

// =====================================================
// scheduled reminder screen
// =====================================================
void drawAlert(){
 screen=ALERT;
 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,50,ILI9341_RED);

 bnC(BN_MED_TIME,BN_MED_TIME_W,BN_MED_TIME_H,0,11,320,ILI9341_WHITE);

 if(isTest){
  const uint8_t*sb;int sw,sh;
  shiftBitmap(activeMed,sb,sw,sh);
  bnC(sb,sw,sh,0,75,320,ILI9341_YELLOW);
 }else{
  const uint8_t*mb;int mw,mh;
  medBitmap(activeUser,activeMed,mb,mw,mh);
  bnC(mb,mw,mh,0,72,320,ILI9341_YELLOW);
 }

 if(reminderNumber==1)
  bn(20,112,BN_FIRST_REMINDER,BN_FIRST_REMINDER_W,BN_FIRST_REMINDER_H,ILI9341_WHITE);
 else
  bn(20,112,BN_SECOND_REMINDER,BN_SECOND_REMINDER_W,BN_SECOND_REMINDER_H,ILI9341_WHITE);

 bn(20,141,BN_CONFIRM,BN_CONFIRM_W,BN_CONFIRM_H,ILI9341_WHITE);

 drawMenuBtn(takenBtn,BN_TAKEN,BN_TAKEN_W,BN_TAKEN_H,ILI9341_GREEN);
 drawMenuBtn(skipBtn,BN_SKIP,BN_SKIP_W,BN_SKIP_H,ILI9341_RED);
}

void startReminder(int u,int m,bool test){
 activeUser=u;
 activeMed=m;
 isTest=test;
 actionMode=NORMAL_DOSE;

 reminderNumber=1;
 waitingForVoice=true;
 lastDisplayed=-999;
 reminderDoorOpen=false;
 pendingServoOpen=false;

 // Safety: the correct door is definitely CLOSED before the reminder melody.
 closeAllDoors();

 drawAlert();

 // FIRST melody only: after all 6 notes finish, updateMelody() opens
 // the correct door exactly once.
 startAttentionVoice(firstTrack[m],true);
}

void saveScheduledAction(int status){
 DateTime n=rtc.now();

 if(isTest){
  testStatus[activeMed]=status;
  drawTestMenu();
  return;
 }

 realStatus[activeUser][activeMed]=status;
 actionMinutes[activeUser][activeMed]=n.hour()*60+n.minute();
 medicineHistoryVisible[activeUser][activeMed]=true;
 triggered[activeUser][activeMed]=true;
 persistMedicineState();

 int u=activeUser;
 drawProfile(u);
}

void medicineTaken(){
 // Close the compartment as soon as the user confirms taking it.
 closeDoorForMedicine(activeMed);
 reminderDoorOpen=false;

 // Scheduled TAKEN gets attention melody + taken voice.
 startAttentionVoice(TAKEN_TRACK,false);
 saveScheduledAction(TAKEN);
}

void medicineSkipped(){
 closeDoorForMedicine(activeMed);
 reminderDoorOpen=false;
 startAttentionVoice(SKIP_TRACK,false);
 saveScheduledAction(SKIPPED);
}

void medicineMissed(){
 // The response window has ended: close the medicine door.
 closeDoorForMedicine(activeMed);
 reminderDoorOpen=false;

 startAttentionVoice(missedTrack[activeMed],false);

 if(isTest){
  testStatus[activeMed]=MISSED;
  drawTestMenu();
 }else{
  DateTime n=rtc.now();
  realStatus[activeUser][activeMed]=MISSED;
  actionMinutes[activeUser][activeMed]=n.hour()*60+n.minute();
  medicineHistoryVisible[activeUser][activeMed]=true;
  triggered[activeUser][activeMed]=true;
  persistMedicineState();
  int u=activeUser;
  drawProfile(u);
 }
}

void updateAlert(){
 if(waitingForVoice)return;

 unsigned long wait=isTest?TEST_WAIT:REAL_WAIT;
 unsigned long e=millis()-reminderStart;
 long r=e>=wait?0:(wait-e+999)/1000;

 if(r!=lastDisplayed){
  lastDisplayed=r;
  tft.fillRect(170,135,130,28,ILI9341_BLACK);
  tft.setTextSize(2);tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(185,141);
  tft.print(countText(r));
 }

 if(e>=wait){
  if(reminderNumber==1){
   reminderNumber=2;
   waitingForVoice=true;
   drawAlert();
   pendingServoOpen=false;
   startAttentionVoice(secondTrack[activeMed],false);
  }else{
   medicineMissed();
  }
 }
}

// =====================================================
// test user
// =====================================================
void drawTestMenu(){
 screen=TEST_MENU;
 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_ORANGE);

 bn(7,14,BN_TEST_USER,BN_TEST_USER_W,BN_TEST_USER_H,ILI9341_BLACK);
 drawBack();

 Button*shiftButtons[3]={&morning,&noonBtn,&night};

 for(int m=0;m<3;m++){
  const uint8_t*sb;
  int sw,sh;
  shiftBitmap(m,sb,sw,sh);

  // Shift selection area: starts the normal test reminder countdown.
  tft.fillRoundRect(
   shiftButtons[m]->x,shiftButtons[m]->y,
   shiftButtons[m]->w,shiftButtons[m]->h,7,
   ILI9341_DARKCYAN
  );
  tft.drawRoundRect(
   shiftButtons[m]->x,shiftButtons[m]->y,
   shiftButtons[m]->w,shiftButtons[m]->h,7,
   ILI9341_WHITE
  );
  bn(
   shiftButtons[m]->x+10,
   shiftButtons[m]->y+7,
   sb,sw,sh,
   ILI9341_WHITE
  );

  // Direct door controls.
  drawBanglaBtn(
   testOpenBtn[m],
   SV_OPEN,SV_OPEN_W,SV_OPEN_H,
   ILI9341_GREEN
  );

  drawBanglaBtn(
   testCloseBtn[m],
   SV_CLOSE,SV_CLOSE_W,SV_CLOSE_H,
   ILI9341_RED
  );
 }

 // Small Bangla instruction.
// wbnC(
 // WBN_SELECT_SHIFT,WBN_SELECT_SHIFT_W,WBN_SELECT_SHIFT_H,
 // 0,219,320,ILI9341_WHITE
// );

tft.setTextColor(ILI9341_WHITE);
tft.setTextSize(2);
tft.setCursor(10, 220);
tft.print("Select OPEN or CLOSE");


}

void startTest(int m){
 selectedShift=m;
 testStartTime=millis();
 lastDisplayed=-1;
 screen=TEST_COUNTDOWN;

 tft.fillScreen(ILI9341_BLACK);
 tft.fillRect(0,0,320,48,ILI9341_ORANGE);
 drawBack();

 const uint8_t*sb;int sw,sh;
 shiftBitmap(m,sb,sw,sh);
 bn(8,15,sb,sw,sh,ILI9341_BLACK);

 bnC(BN_REMINDER_IN,BN_REMINDER_IN_W,BN_REMINDER_IN_H,0,128,320,ILI9341_CYAN);
}

void updateTestCountdown(){
 unsigned long e=millis()-testStartTime;
 long r=5-e/1000;
 if(r<0)r=0;

 if(r!=lastDisplayed){
  lastDisplayed=r;
  tft.fillRect(105,150,110,32,ILI9341_BLACK);
  tft.setTextSize(3);tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(120,150);
  tft.print(r);tft.print(" s");
 }

 if(e>=TEST_START)
  startReminder(-1,selectedShift,true);
}

// =====================================================
// RTC alarms
// =====================================================
void checkRealAlarm(){
 if(screen==ALERT)return;

 DateTime n=rtc.now();

 for(int u=0;u<3;u++){
  for(int m=0;m<3;m++){
   Medicine d=users[u].med[m];

   if(n.hour()==d.hour&&
      n.minute()==d.minute&&
      !triggered[u][m]&&
      realStatus[u][m]==READY){

    triggered[u][m]=true;
    startReminder(u,m,false);
    return;
   }
  }
 }
}

void resetDay(){
 DateTime n=rtc.now();
 uint32_t today=makeDateCode(n);

 if(runtimeDateCode==0)
  runtimeDateCode=today;

 if(today!=runtimeDateCode){
  runtimeDateCode=today;
  closeAllDoors();
  resetMedicineRam();
  medicinePrefs.clear();
  persistMedicineState();

  // If a medicine page is open at midnight, refresh it cleanly.
  if(screen==HISTORY)drawHistory();
  else if(screen==SCHEDULE)drawSchedule();
  else if(screen==PROFILE){
   lastProfileMedicine=-99;
   lastProfileButtonState=-99;
   updateProfile();
  }
 }
}

// =====================================================
// touch
// =====================================================
bool getTouch(int&x,int&y){
 if(!ts.touched())return false;

 TS_Point p=ts.getPoint();

 x=constrain(map(p.x,TS_LEFT,TS_RIGHT,0,320),0,319);
 y=constrain(map(p.y,TS_TOP,TS_BOTTOM,0,240),0,239);

 return true;
}

void handleTouch(int x,int y){
 if(screen==HOME){
  if(inside(u1,x,y))drawProfile(0);
  else if(inside(u2,x,y))drawProfile(1);
  else if(inside(u3,x,y))drawProfile(2);
  else if(inside(testBtn,x,y))drawTestMenu();
 }

 else if(screen==PROFILE){
  if(inside(backBtn,x,y))drawHome();
  else if(inside(scheduleBtn,x,y))drawSchedule();
  else if(inside(historyBtn,x,y))drawHistory();
  else if(inside(weightBtn,x,y))drawWeightMenu();
  else if(inside(manualBtn,x,y)){
   DateTime n=rtc.now();
   int od=overdueDose(selectedUser,n);
   long rem;
   int nx=nextUpcoming(selectedUser,n,rem);

   if(od>=0)drawConfirmManual(od,LATE_DOSE);
   else if(nx>=0)drawConfirmManual(nx,EARLY_DOSE);
  }
 }

 else if(screen==SCHEDULE){
  if(inside(backBtn,x,y))drawProfile(selectedUser);
 }

 else if(screen==HISTORY){
  if(inside(backBtn,x,y))drawProfile(selectedUser);
  else if(inside(deleteMedicineHistoryBtn,x,y))
   drawClearHistoryConfirm(HISTORY);
 }

 else if(screen==WEIGHT_MENU){
  if(inside(backBtn,x,y))drawProfile(selectedUser);
  else if(inside(measureWeightBtn,x,y))drawWeightMeasure();
  else if(inside(weightHistoryBtn,x,y))drawWeightHistory();
 }

 else if(screen==WEIGHT_MEASURE){
  if(inside(backBtn,x,y))drawWeightMenu();
  else if(inside(zeroWeightBtn,x,y))tareWeightScale();
  else if(inside(saveWeightBtn,x,y))saveCurrentWeight();
  else if(inside(measureHistoryBtn,x,y))drawWeightHistory();
 }

 else if(screen==WEIGHT_HISTORY){
  if(inside(backBtn,x,y))drawWeightMenu();
  else if(inside(graphBtn,x,y))drawWeightGraph();
  else if(inside(deleteWeightHistoryBtn,x,y))
   drawClearHistoryConfirm(WEIGHT_HISTORY);
 }

 else if(screen==WEIGHT_GRAPH){
  if(inside(backBtn,x,y))drawWeightHistory();
 }


 else if(screen==CLEAR_HISTORY_CONFIRM){
  if(inside(yesBtn,x,y))executeClearAllHistory();
  else if(inside(noBtn,x,y))returnFromClearHistory();
 }

 else if(screen==CONFIRM_MANUAL){
  if(inside(backBtn,x,y)||inside(noBtn,x,y))
   drawProfile(selectedUser);
  else if(inside(yesBtn,x,y))
   startManualTakeWindow();
 }

 else if(screen==MANUAL_TAKE){
  if(inside(backBtn,x,y)||inside(cancelBtn,x,y)){
   // No medicine taken -> no history/status/audio.
   int u=activeUser;
   drawProfile(u);
  }
  else if(inside(manualTakenBtn,x,y)){
   confirmManualTaken();
  }
 }

 else if(screen==TEST_MENU){
  if(inside(backBtn,x,y))drawHome();
  else if(inside(testOpenBtn[0],x,y))openDoor(0);
  else if(inside(testCloseBtn[0],x,y))closeDoor(0);
  else if(inside(testOpenBtn[1],x,y))openDoor(1);
  else if(inside(testCloseBtn[1],x,y))closeDoor(1);
  else if(inside(testOpenBtn[2],x,y))openDoor(2);
  else if(inside(testCloseBtn[2],x,y))closeDoor(2);
  else if(inside(morning,x,y))startTest(0);
  else if(inside(noonBtn,x,y))startTest(1);
  else if(inside(night,x,y))startTest(2);
 }

 else if(screen==TEST_COUNTDOWN){
  if(inside(backBtn,x,y))drawTestMenu();
 }

 else if(screen==ALERT){
  if(inside(takenBtn,x,y))medicineTaken();
  else if(inside(skipBtn,x,y))medicineSkipped();
 }
}

// =====================================================
// setup / loop
// =====================================================
void setup(){
 Serial.begin(115200);

 pinMode(BUZZER_PIN,OUTPUT);
 digitalWrite(BUZZER_PIN,LOW);

 // Initialize all three medicine doors first.
 // They are forced to their calibrated CLOSED positions.
 setupServos();

 tft.begin();
 tft.setRotation(1);

 ts.begin();
 ts.setRotation(1);

 Wire.begin(RTC_SDA,RTC_SCL);

 if(!rtc.begin()){
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_RED);
  tft.setTextSize(2);
  tft.setCursor(60,100);
  tft.print("RTC ERROR");
  while(1)delay(100);
 }

 if(rtc.lostPower())
  rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));

 // Restore today's medicine state/history and all saved weight history
 // from ESP32 internal flash (NVS / Preferences).
 loadMedicineState();
 loadWeightHistory();
 Serial.println("Saved medicine + weight history loaded from ESP32 flash.");

 mp3Serial.begin(9600,SERIAL_8N1,MP3_RX_PIN,MP3_TX_PIN);
 delay(500);

 if(player.begin(mp3Serial)){
  player.volume(30);
  Serial.println("MP3 READY");
 }else{
  Serial.println("MP3 NOT FOUND");
 }

 // ---------------------------------------------------
 // HX711 initialization
 // GPIO34 = DT, GPIO14 = SCK
 // Keep the weighing platform EMPTY during startup.
 //
 // Do NOT use a one-shot scale.is_ready() here.
 // DOUT can legitimately be HIGH between conversions.
 // ---------------------------------------------------
 scale.begin(HX711_DT,HX711_SCK);
 scale.set_scale(calibrationFactor);
 scale.power_up();

 // Bangla startup screen
 tft.fillScreen(ILI9341_BLACK);
 wbnC(WBN_SCALE,WBN_SCALE_W,WBN_SCALE_H,0,72,320,ILI9341_WHITE);
 wbnC(WBN_KEEP_EMPTY,WBN_KEEP_EMPTY_W,WBN_KEEP_EMPTY_H,0,105,320,ILI9341_YELLOW);
 wbnC(WBN_PREPARING,WBN_PREPARING_W,WBN_PREPARING_H,0,135,320,ILI9341_CYAN);

 Serial.println("Keep scale EMPTY.");
 Serial.println("Waiting for HX711/load cells...");

 // Same settling time as your confirmed standalone code.
 delay(8000);

 // Wait for a real conversion rather than checking only one instant.
 hx711Ready=scale.wait_ready_timeout(3000);

 if(hx711Ready){
  Serial.println("HX711 found. Taring...");

  tft.fillRect(0,132,320,30,ILI9341_BLACK);
  wbnC(WBN_ZEROING,WBN_ZEROING_W,WBN_ZEROING_H,0,138,320,ILI9341_YELLOW);

  // Same tare value as your confirmed standalone code.
  scale.tare(50);

  Serial.println("HX711 READY");
 }else{
  Serial.println("HX711 NOT READY at startup.");
  Serial.println("The program will retry automatically on the weight screen.");

  tft.fillRect(0,132,320,30,ILI9341_BLACK);
  wbnC(
   WBN_SCALE_NOT_READY,
   WBN_SCALE_NOT_READY_W,WBN_SCALE_NOT_READY_H,
   0,138,320,ILI9341_RED
  );

  delay(1200);
 }

 drawHome();
}

void loop(){
 updateMelody();

 if(screen==WEIGHT_MEASURE)
  updateWeightMeasure();

 if(millis()-lastClock>=1000){
  lastClock=millis();

  resetDay();
  checkRealAlarm();

  if(screen==HOME)updateClock();
  else if(screen==PROFILE)updateProfile();
 }

 if(screen==TEST_COUNTDOWN)
  updateTestCountdown();

 if(screen==ALERT)
  updateAlert();

 if(screen==MANUAL_TAKE)
  updateManualTake();

 int x,y;

 if(getTouch(x,y)){
  handleTouch(x,y);

  while(ts.touched())
   delay(5);

  delay(60);
 }
}
