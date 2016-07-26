/*
  Nachführung
 
 Version: 0.90
 Datum: 2016-02-17
 Autor: C. Pohl
 */


///////////////////////////////////////////////
#define DEBUG

///////////////////////////////////////////////
// Libraries
// LCD
#include <LiquidCrystal.h>
// EEPROM
#include <EEPROM.h>

///////////////////////////////////////////////
// Von der Hardware bestimmt: 
// F_INT: Aufruffrequenz des Interrupt in Hz (int)
// F_MAX: Maximalfrequenz für den Motor in Hz (int)
// F_E: Motortakt für Kompensation der Erddrehung in Hz (float)
// siderischer Tag: 86.164,099s => 
// F_E = 1/(86164,099/144/120/48) (144 Zähne Schneckengetriebe, 1:120 Motorübersetzung, 48 Schritte pro Motorumdrehung)

#define F_INT 10000         // Hz; Interruptfequenz (extern)
//fullstep
#define F_MAX 260           // Hz; Maximle Stepperfrequenz
#define F_E 9.626283        // Hz; Taktfrequenz für Nachführung

// Digital PIN
// Int.2                0   // [IN]
#define PIN_KAM         1   // [OUT] Kammeraauslöser
#define PIN_M_RA_1     13   // [OUT] RA-Stepper 1
#define PIN_M_RA_2     A0   // [OUT] RA-Stepper 2
#define PIN_M_DC_1     A1   // [OUT] DC-Stepper 1
#define PIN_M_DC_2     A2   // [OUT] DC-Stepper 2
#define PIN_LED_ROT     6   // [OUT] Rotlicht (PWM)
#define PIN_LCD_RS      7   // LCD
#define PIN_LCD_EN      8   // LCD
#define PIN_LCD_D4      9   // LCD
#define PIN_LCD_D5     10   // LCD
#define PIN_LCD_D6     11   // LCD
#define PIN_LCD_D7     12   // LCD
#define PIN_UP          3   // [IN] Taster UP/SELECT
#define PIN_DN          2   // [IN] Taster DOWN
#define PIN_NX          4   // [IN] Taster NEXT
#define PIN_BK          5   // [IN] Taster BACK
#define PIN_POTI_RA    A3   // [IN] Poti RA-Manuell (analog)
#define PIN_POTI_DC    A4   // [IN] Poti DC-Manuelll (analog)
#define PIN_POTI_ROT   A5   // [IN] Poti Helligkeit Rotlicht (analog)

#define NUM_INT 2           // Interruptnummer (micro: PIN 0)

#define BUTTON_UP 0x08
#define BUTTON_DN 0x04
#define BUTTON_NX 0x02
#define BUTTON_BK 0x01

///////////////////////////////////////////////
// Globale Variablen für Interrupt
// 0=RA 1=DC 2=countdown 3=timer
volatile byte dir[2]         = {1,1}; // motor direction  +1 oder -1 
volatile int  maxcount[4]    = {0,0,0,0}; // maxcount bis aktion
volatile int  count[4]       = {0,0,0,0}; // count
volatile byte step_number[2] = {0,0}; // in diesem Schritt steht der Motor
// Countdown timer
volatile byte cur_countdown=0;
volatile byte countdown_update=0;
// Timer
volatile int cur_timer=0;
volatile byte timer_update=0;

///////////////////////////////////////////////
// Globale Variablen
const byte motor_pin[2][4] = 
{
  { PIN_M_RA_1, PIN_M_RA_2 },
  { PIN_M_DC_1, PIN_M_DC_2 }
};
// Pinzustände für Schrittmotor-Schritte
const byte steps[4][2] = 
{ 
  { 1, 0 }, // 0
  { 1, 1 }, // 1
  { 0, 1 }, // 2
  { 0, 0 }  // 3
};

///////////////////////////////////////////////
// LCD
LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);

///////////////////////////////////////////////
// cur_cursor
// 00 00 0000
// |  |  column (0-15)
// |  row (0-1)
// seite (0-3)
//
///////////////////////////////////////////////
// Tasten
// cur_cursor lesbar: seite/row/col
// Timer steht, Zeit einstellen
//  0/0/00 10er Minuten   up/select: +10min      down: -10min      next: 0/0/01    back: 0/1/00
//  0/0/01  1er Minuten   up/select:  +1min      down:  -1min      next: 0/0/03    back: 0/0/00
//  0/0/03 10er Sekunden  up/select: +10sec      down: -10sec      next: 0/0/04    back: 0/0/01
//  0/0/04  1er Sekunden  up/select:  +1sec      down:  -1sec      next: 0/0/08    back: 0/0/03
//  0/0/08 Start          up/select: 1/0/08      down: n/a         next: 0/1/00    back: 0/0/04
//  0/1/00 Einstellungen  up/select: 3/0/00      down: n/a         next: 0/0/00    back: 0/0/08
// Timer steht, Countdown läuft
//  1/0/08 Abbruch        up/select: 0/0/00      down: n/a         next: n/a       back: 0/0/00
// Timer läuft
//  2/0/08 Abbruch        up/select: 0/0/00      down: n/a         next: n/a       back: 0/0/00
// Timer steht, Einstellungen
//  3/0/00 Parameter X    up/select: Parameter + down: Parameter - next: 3/0/14    back: 0/0/00
//  3/0/14 Wert X 10er    up/select: Wert +10    down: Wert -10    next: 3/0/15    back: 3/0/00
//  3/0/15 Wert X  1er    up/select: Wert +1     down: Wert -1     next: 3/0/00    back: 3/0/14
// 

///////////////////////////////////////////////
// Gespeicherte Parameter
// Aktuell gewählter Wert.
byte cur_param = 0;
// Wert, Minimalwert, Maximalwert
volatile byte cur_paramval[4][3] = {
  { 0,0,99 }, // Mindestabweichung aus Mittelstellung Joystick
  { 0,0,99 }, // Mindestwert Rotlichtpoti
  { 0,0,15 }, // Countdown
  { 0,0,1 }
};
// Text Zeile 1, Text Zeile 2
const char paramtext[4][2][17] = {
  { "Joystick min","0 - 99" },
  { "Rotlicht min","0 - 99" },
  { "Countdown",   "0 - 15 sec" },
  { "Nord/Sued",   "N=0, S=1" }
};
// Eingestellter Timer 
int timer=0;

///////////////////////////////////////////////
// Runtime
// Cursorposition
byte cur_cursor=0x00;

///////////////////////////////////////////////

void setup() {    
#ifdef DEBUG
  Serial.begin(9600);
#endif

  pinMode(PIN_KAM,OUTPUT);
  pinMode(PIN_LED_ROT,OUTPUT);
  pinMode(PIN_M_RA_1,OUTPUT);
  pinMode(PIN_M_RA_2,OUTPUT);
  pinMode(PIN_M_DC_1,OUTPUT);
  pinMode(PIN_M_DC_2,OUTPUT);
  pinMode(PIN_POTI_RA,INPUT);
  pinMode(PIN_POTI_DC,INPUT);
  pinMode(PIN_POTI_ROT,INPUT);
  pinMode(PIN_BK,INPUT);
  pinMode(PIN_NX,INPUT);
  pinMode(PIN_UP,INPUT);
  pinMode(PIN_DN,INPUT);

  lcd.begin(16, 2);
  delay(2000);
  lcd.clear();
  delay(2000);
  lcd.blink();
  lcd.noAutoscroll();
  
  // Parameter aus EEPROM lesen , Werte sind byte, darum nichts Besonderes zu beachten
  for(int i=0; i<4; i++) {
    cur_paramval[i][0] = EEPROM.read(i);
  }

  // lcd startseite aufbauen
  pPage0();

  //attachInterrupt(NUM_INT, inthandler, CHANGE);
  attachInterrupt(NUM_INT, inthandler, RISING);
} 

///////////////////////////////////////////////

void loop() {
  
  ///////////////////////////////////////////////
  // alte Werte speichern um minimale LCD Aktualisierung 
  // zu erreichen
  ///////////////////////////////////////////////
  byte st_cursor = cur_cursor;
  byte st_param = cur_param;  
  byte st_paramval = cur_paramval[cur_param][0];

  ///////////////////////////////////////////////
  // Rotlicht
  ///////////////////////////////////////////////
  int pRot=1023-analogRead(PIN_POTI_ROT); // poti ist verkehrt herum angeschlossen
  if(pRot <= cur_paramval[1][0]) {  // mindesthelligkeit
    digitalWrite(PIN_LED_ROT,0); 
  } 
  else { 
    analogWrite(PIN_LED_ROT,pRot/4); //analogRead: 0..1023; analogWrite: 0..255
  }            

  ///////////////////////////////////////////////
  // Motorsteuerung
  ///////////////////////////////////////////////
  int mitte = 512;                        // Mittelstellung der Potis
  int pRA = analogRead(PIN_POTI_RA);      // Potis einlesen 0..1023
  int pDC = analogRead(PIN_POTI_DC);      // Potis einlesen 0..1023
  int xRA = pRA-mitte;                    // Abweichung aus der Mittelstellung
  int xDC = pDC-mitte;                    // Abweichung aus der Mittelstellung

  if(abs(xRA) > cur_paramval[0][0]) {     // Mindestabweichung aus der Mittelstellung, mit dieser Abfrage ist auch p_rel=0 abgefangen
    float p_rel = (float)xRA/mitte;       // Potistellung auf -1..0..+1
    if(p_rel >= 0 ) {                     // Richtung
      dir[0] = (cur_paramval[3][0]==0 ? 1 : -1); 
    } 
    else { 
      dir[0] = (cur_paramval[3][0]==0 ? -1 : 1); 
    };
    float f_RA = abs(F_E+(F_MAX-F_E)*p_rel);     // Frequenz
    maxcount[0] = (int)(F_INT/f_RA);
    pManu(0,1);
  }
  else {
    maxcount[0] = (int)(F_INT/F_E);
    dir[0] = (cur_paramval[3][0]==0 ? 1 : -1); 
    pManu(0,0);
  }
  if(abs(xDC) > cur_paramval[0][0]) {     // Mindestabweichung aus der Mittelstellung, mit dieser Abfrage ist auch p_rel=0 abgefangen
    float p_rel = (float)xDC/mitte;       // Potistellung auf -1..0..+1
    if(p_rel >= 0) {                      // Richtung
      dir[1] = 1; 
    } 
    else { 
      dir[1] = -1; 
    };
    float f_DC = abs(F_MAX*p_rel);        // Frequenz
    maxcount[1] = (int)(F_INT/f_DC);
    pManu(1,1);
  }
  else {
    maxcount[1] = 0;
    pManu(1,0);
  }
#ifdef DEBUG
  Serial.print("maxcount 0: ");
  Serial.print(maxcount[0]);
  Serial.print(" 1: ");
  Serial.println(maxcount[1]);
#endif
  ///////////////////////////////////////////////
  // Tasten abfragen
  ///////////////////////////////////////////////
  byte buttons = digitalRead(PIN_UP) << 3 | digitalRead(PIN_DN) << 2  | digitalRead(PIN_NX) << 1 | digitalRead(PIN_BK);

  if (buttons) {
#ifdef DEBUG
  Serial.print("Button - ");
  Serial.print(buttons,HEX);
  Serial.print(" | cur_cursor - ");
  Serial.println(cur_cursor,HEX);
#endif
    delay(100); // Tastenprell abfangen
    switch(cur_cursor) {  // aufbau cur_cursor: siehe oben
// Timer steht, Zeit einstellen
//  0/0/00 10er Minuten   up/select: +10min      down: -10min      next: 0/0/01    back: 0/1/00
//  0/0/01  1er Minuten   up/select:  +1min      down:  -1min      next: 0/0/03    back: 0/0/00
//  0/0/03 10er Sekunden  up/select: +10sec      down: -10sec      next: 0/0/04    back: 0/0/01
//  0/0/04  1er Sekunden  up/select:  +1sec      down:  -1sec      next: 0/0/08    back: 0/0/03
//  0/0/08 Start          up/select: 1/0/08      down: n/a         next: 0/1/00    back: 0/0/04
//  0/1/00 Einstellungen  up/select: 3/0/00      down: n/a         next: 0/0/00    back: 0/0/08
      case 0x00:  
        if (buttons & BUTTON_UP) {
          timer+=600;
        }
        else if (buttons & BUTTON_DN) {
          timer-=600;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0x10; // 0001 0000
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0x01; // 0000 0001  
        } 
        if(timer >= 0 && timer < 6000) { cur_timer=timer; timer_update=1; } else { timer=cur_timer; }
      break;
      case 0x01:
        if (buttons & BUTTON_UP) {
          timer += 60;
        }
        else if (buttons & BUTTON_DN) {
          timer -= 60;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0x00; // 0000 0000
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0x03; // 0000 0011  
        } 
        if(timer >= 0 && timer < 6000) { cur_timer=timer; timer_update=1; } else { timer=cur_timer; }
      break;
      case 0x03:
        if (buttons & BUTTON_UP) {
          timer += 10;
        }
        else if (buttons & BUTTON_DN) {
          timer -=10;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0x01; // 0000 0001
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0x04; // 0000 0100  
        } 
        if(timer >= 0 && timer < 6000) { cur_timer=timer; timer_update=1; } else { timer=cur_timer; }
      break;
      case 0x04:
        if (buttons & BUTTON_UP) {
          timer += 1;
        }
        else if (buttons & BUTTON_DN) {
          timer -= 1;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0x03; // 0000 0011
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0x08; // 0000 1000  
        }
        if(timer >= 0 && timer <6000 ) { cur_timer=timer; timer_update=1; } else { timer=cur_timer; }
      break;
      case 0x08:
        if (buttons & BUTTON_BK) {
          cur_cursor = 0x04; // 0000 0100
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0x10; // 0001 0000  
        } 
        else if (buttons & BUTTON_UP) {
          if(cur_paramval[2][0] > 0) {
            maxcount[2] = F_INT; // Countdown starten
            cur_countdown = cur_paramval[2][0]; // Startwert setzen
            cur_cursor = 0x48;
          }
          else if(cur_timer > 0) {
            maxcount[3] = F_INT; // Timer starten
            cur_cursor = 0x88;
          }
        }
      break;
      case 0x10:
        if (buttons & BUTTON_BK) {
          cur_cursor = 0x08; // 0000 0008
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0x00; // 0000 0000  
        } 
        else if (buttons & BUTTON_UP) {
          cur_cursor = 0xc0; // 1100 0000
        }
      break;
// Timer steht, Countdown läuft
//  1/0/08 Abbruch        up/select: 0/0/00      down: n/a         next: n/a       back: 0/0/00
      case 0x48:
        if (buttons & (BUTTON_UP | BUTTON_BK)) {
          maxcount[2]=0; // Countdown stoppen
          cur_cursor = 0x00; // 0000 0000
        }
      break;
// Timer läuft
//  2/0/08 Abbruch        up/select: 0/0/00      down: n/a         next: n/a       back: 0/0/00
      case 0x88:
        if (buttons & (BUTTON_UP | BUTTON_BK)) {
          maxcount[3]=0; // Timer stoppen
          cur_timer=timer; // Timer zurücksetzen
          cur_cursor = 0x00; // 0000 0000
        }
      break;
// Timer steht, Einstellungen
//  3/0/00 Parameter X    up/select: Parameter + down: Parameter - next: 3/0/13    back: 0/0/00
//  3/0/14 Wert X 10er    up/select: Wert +10    down: Wert -10    next: 3/0/14    back: 3/0/00
//  3/0/15 Wert X  1er    up/select: Wert +1     down: Wert -1     next: 3/0/00    back: 3/0/13
      case 0xc0:
        if (buttons & BUTTON_UP) {
          cur_param += 1;
        }
        else if (buttons & BUTTON_DN) {
          cur_param -= 1;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0x00; // 0000 0000
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0xcd; // 1100 1101
        } 
        if(cur_param < 0 || cur_param > 3 ) { cur_param = st_param; }
      break;
      case 0xcd:
        if (buttons & BUTTON_UP) {
          cur_paramval[cur_param][0] += 10;
        }
        else if (buttons & BUTTON_DN) {
          cur_paramval[cur_param][0] -= 10;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0xc0; // 1100 0000
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0xce; // 1100 1110
        } 
        if(cur_paramval[cur_param][0] < cur_paramval[cur_param][1] || cur_paramval[cur_param][0] > cur_paramval[cur_param][2]) {
          cur_paramval[cur_param][0] = st_paramval % cur_paramval[cur_param][2]; // modulo, um maxwertänderungen abzufangen
        }
      break;
      case 0xce:
        if (buttons & BUTTON_UP) {
          cur_paramval[cur_param][0] += 1;
        }
        else if (buttons & BUTTON_DN) {
          cur_paramval[cur_param][0] -= 1;
        }
        else if (buttons & BUTTON_BK) {
          cur_cursor = 0xcd; // 1100 1101
        }
        else if (buttons & BUTTON_NX) {
          cur_cursor = 0xc0; // 1100 0000
        } 
        if(cur_paramval[cur_param][0] < cur_paramval[cur_param][1] || cur_paramval[cur_param][0] > cur_paramval[cur_param][2]) {
          cur_paramval[cur_param][0] = st_paramval % cur_paramval[cur_param][2]; // modulo, um maxwertänderungen abzufangen
        }
      break;
    }
  }

  ///////////////////////////////////////////////
  // Timer/Countdown
  ///////////////////////////////////////////////

  if(cur_cursor == 0x48) {   // Countdown läuft
    if(cur_countdown <= 0) { // Countdown abgelaufen
      maxcount[2] = 0;       // Countdown stoppen
      cur_timer = timer;     // Startwert setzen
      maxcount[3] = F_INT;   // Timer starten
      cur_cursor = 0x88;
    }
  }
  else if(cur_cursor == 0x88) { // Timer läuft
    if(cur_timer <= 0) {        // Timer abgelaufen
      maxcount[3] = 0;          // Timer stoppen
      cur_timer = timer;        // Timer zurücksetzen
      cur_cursor = 0x00;
    }
  }

  ///////////////////////////////////////////////
  // Anzeige aktualisieren
  ///////////////////////////////////////////////
  if((cur_cursor & 0xc0) != (st_cursor & 0xc0)) { // Seite geändert
#ifdef DEBUG
  Serial.print(" | neu cur_cursor - ");
  Serial.println(cur_cursor,HEX);
#endif
    int page = cur_cursor & 0xc0;
    switch(page) {
      case 0x00:
        pPage0();
        countdown_update = 0;
      break;
      case 0x40:
        pPage1();
      break;
      case 0x80:
        pPage2();
      break;
      case 0xc0:
        pPage3();
      break;
    }
  }
  else if(countdown_update == 1) { // countdown
#ifdef DEBUG
  Serial.print(" | cur_countdown - ");
  Serial.println(cur_countdown,HEX);
#endif
    pCountdown();
    countdown_update = 0;
  }
  else if(timer_update == 1) { // timer
#ifdef DEBUG
  Serial.print(" | cur_timer - ");
  Serial.println(cur_timer,HEX);
#endif
    pTimer();
    timer_update = 0;
  }
  else if(cur_param != st_param) { // parameter
    pPage3();
  }
  else if(cur_paramval[cur_param][0] != st_paramval) { // parameterwert
    char p[3];
    byte x = EEPROM.read(cur_param);
    sprintf(p,"%02d",cur_paramval[cur_param][0]);
    lcd.setCursor(13,0);
    lcd.print(p);
    if(x != cur_paramval[cur_param][0]) {
      EEPROM.write(cur_param,cur_paramval[cur_param][0]);
    }
  }
  lcd.setCursor(cur_cursor & 0x0f, cur_cursor & 0x30);

#ifdef DEBUG
  delay(100); // Adruino micro Besonderheit
#endif
}

// Interrupthandler
void inthandler(void) {
  if(maxcount[0] != 0 ) {
    if(count[0] >= maxcount[0]) {
      dostep(0); // 1 Schritt in Richtung
      count[0] = 0;
    }
    else {
      count[0] += 1;
    }
  }
  if(maxcount[1] != 0) {
    if(count[1] >= maxcount[1]) {
      dostep(1); // 1 Schritt in Richtung
      count[1] = 0;
    }
    else {
      count[1] += 1;
    }
  }
  if(maxcount[2] != 0) {
    if(count[2] >= maxcount[2]) {
      cur_countdown -= 1;
      countdown_update = 1;
      count[2] = 0;
#ifdef DEBUG
  Serial.print(" | countdown - ");
  Serial.println(cur_countdown,HEX);
#endif
    }
    else {
      count[2] += 1;
    }
  }
  if(maxcount[3] != 0) {
    if(count[3] >= maxcount[3]) {
      cur_timer -= 1;
      timer_update = 1;
      count[3] = 0;
#ifdef DEBUG
  Serial.print(" | timer - ");
  Serial.println(cur_timer,HEX);
#endif
    }
    else {
      count[3] += 1;
    }
  }
}

// Ein Motorschritt
void dostep(byte motor)
{ 
  step_number[motor] = (step_number[motor] + dir[motor]) % 4;
  digitalWrite(motor_pin[motor][0],steps[step_number[motor]][0]);
  digitalWrite(motor_pin[motor][1],steps[step_number[motor]][1]);
}

// Formatierte Ausgabe des Timers
void pTimer() {
  char s[6];
  sprintf(s,"%02d:%02d",(int)cur_timer/60,(int)cur_timer%60);
  lcd.setCursor(0,0);
  lcd.print(s);
}

// Countdown-Balken
void pCountdown() {
  byte i=cur_countdown;
  lcd.setCursor(0,1);
  while(i>0) {
    i--;
    lcd.print("=");
  }
  lcd.print(" ");
}

// Zeiteinstellung, Start und Einstellung
void pPage0() {
  pClean();
  pTimer();
  lcd.setCursor(8,0);
  lcd.print("Start");
  lcd.setCursor(0,1);
  lcd.print("Einstellungen");
  digitalWrite(PIN_KAM, LOW); // Shutter zu
}

// Countdown läuft
void pPage1() {
  pClean();
  pTimer();
  lcd.setCursor(8,0);
  lcd.print("Abbruch");
  pCountdown();
  digitalWrite(PIN_KAM, LOW); // Shutter zu
}

// Timer läuft
void pPage2() {
  pClean();
  pTimer();
  lcd.setCursor(8,0);
  lcd.print("Abbruch");
  digitalWrite(PIN_KAM, HIGH); // Shutter auf
}

// Einstellungen
void pPage3() {
  char p[3];
  pClean();
  lcd.setCursor(0,0);
  lcd.print(paramtext[cur_param][0]);
  sprintf(p,"%02d",cur_paramval[cur_param][0]);
  lcd.setCursor(13,0);
  lcd.print(p);
  lcd.setCursor(0,1);
  lcd.print(paramtext[cur_param][1]);
  digitalWrite(PIN_KAM, LOW); // Shutter zu
}

void pClean() {
  lcd.setCursor(0,0);
  lcd.print("                ");
  lcd.setCursor(0,1);
  lcd.print("                ");
}

void pManu(byte ach, byte stat) {
  if(ach == 0) {
    lcd.setCursor(15,0);
    if(stat == 1) {
      lcd.print("R");
    }
    else {
      lcd.print(" ");
    }
  }
  if(ach == 1) {
    lcd.setCursor(15,1);
    if(stat == 1) {
      lcd.print("D");
    }
    else {
      lcd.print(" ");
    }
  }
}
