/* 3x3x3 RGB LED mini Cube test


          2018 Ragnar Aronsen (raron)

     2026.11.13 - Slighest change back to analog input A7 for Arduino Nano
     2023.11.06 - Slighest change to analog input A5 for Atmega328 DIP version
     2018.04.27 - Tinkering with FFT stuff
     2018.04.12 - Messing with the cube again..
     2018.04.02 - n-phase pulser animation. orbits animation
     2018.03.31 - 81 LEDs (27 RGB) with 128 individual PWM levels @ 52.3 Hz achieved
     2018.03.30 - inverted rows to save a chip and a transistor
     2018.03.24 - managed at least 64 PWM levels at 68 Hz
     2018.03.19 - started tinkering

  Directly driven by and connected to an Arduino Nano (or compatible)
  No schematics atm but in short: 
  
  Arduino pins to RGB LED (Common cathode) pinout:
 
    portD   0,3,6 = blue columns  (2.7 k ohm series resistors)
    portD   1,4,7 = green columns (2.7 k ohm series resistors)
    portD/B 2,5,8 = red columns   (470 ohm series resistors)
    
    pin 9-17 = row pins (active-low)
    pin 9 = row 1 upper front, pin 17 = row 9 lower back

               Top
 
              9  12  15
     Front   10  13  16   Back
             11  14  17

                Base (PCB / circuit board)

    
    pin 18 - button (active-low)
    Analog input 5 - mic (preamp)


  TODO:
    make a few main functions selectable by button.
    make a function that goes through the rest (all?) animations successively.
    IF FFT works:
    - a function that changes animation by beat ?
    - change something else by sound?
    - FFT 3D spectrum scroller
    - FFT wall scroller
    - FFT wall wrap (8 frequency bins around cube, center LEDs..something?)
    - more FFT stuff?
    
    Look into custom types               * DONE
    improve ISR                          * DONE
     - faster (no function call)        * DONE
    Improve PWM
     - Look into lower PWM level flickering * DONE !!
         - shuffle row PWM method? * depr
     - Logarithmic brightness?          * testing
    Get rid of delay()                   * DONE
    button debounce                      * DONE

    More cool animations! (show off PWM more)
    - Central LED brightness beats, growing with beats to surrounding LED's (orbit variation?)
    - flame animation !
    - fireworks-ish?
    - Matrix / rain
    - running LEDs                      * DONE 
    - running LEDs with LEDs fade in/out
    - alternate color blinking / fading * DONE
    - spinning plane                    * DONE
    - growing color cubes
    - moving color planes
    - XYZ RGB blocks (+fade?)

    Make a function that goes through all animations
      (separate button function and animation function) * DONE
*/

#include "fix_fft.h"


struct RGB {
  unsigned char r;
  unsigned char g;
  unsigned char b;
};

struct xzmap {
  int x;
  int z;
};

// Some overloaded operators for RGB to simplify some code
inline RGB operator * (const RGB &a, const double &val) {
  return {a.r*val, a.g*val, a.b*val};
}

inline RGB operator * (const double &val, const RGB &a) {
  return {a.r*val, a.g*val, a.b*val};
}

inline RGB operator / (const RGB &a, const double &val) {
  return {a.r/val, a.g/val, a.b/val};
}

inline RGB operator + (const RGB &a, const RGB &b) {
  return {a.r+b.r, a.g+b.g, a.b+b.b};
}

inline RGB operator - (const RGB &a, const RGB &b) {
  return {a.r-b.r, a.g-b.g, a.b-b.b};
}


inline RGB operator | (const RGB &a, const RGB &b) {
  return {a.r|b.r, a.g|b.g, a.b|b.b};
}

inline RGB operator ! (const RGB &a) {
  return {255-a.r, 255-a.g, 255-a.b};
}


RGB cube[3][3][3];      // The Cube matrix. 0,0,0=front bottom left, 2,2,2=back upper right


// Timing stuff
unsigned long currentTime;
unsigned long startTime;
unsigned long runtime;
unsigned long buttonChangeTime =   0;
unsigned long debounceDelay   =   50; // button debounce in ms


// Arduino pinout (Mostly Arduino Nano)
const int firstColPin =  0; // Column outputs
const int firstRowPin =  9; // Row outputs
const int buttonPin   = 18; // function button

// AUDIO PIN DIFFERENCE Nano - Atmega 328 DIP version
//const int audioPin    =  5; // Analog input pin A5 FOR ATMEGA 328 DIP version!
const int audioPin    =  7; // Analog input pin A7


// Get cube Y and Z coordinates for row to be displayed
int gety[9] = { 2, 1, 0, 2, 1, 0, 2, 1, 0 };
int getz[9] = { 0, 0, 0, 1, 1, 1, 2, 2, 2 };

// Hardware column bits connection
// get color output bits for Port D output from cube x (column) position
//int blueBit[3]  = { 0, 3, 6 };
//int greenBit[3] = { 1, 4, 7 };
//int redBit[3]   = { 2, 5, 0 }; // Note!! Last bit is port B not D!

// PWM stuff
const unsigned char PWMres    = 128 ;  // PWM resolution/levels
const unsigned char colorStep = 256 / PWMres ;
const unsigned char colorMax  = 256 - colorStep ;

// Gamma correction stuff
unsigned char gamma[256];   // gamma correction table
const float gammaVal = 1.0; // gamma value (1.0 = no correction, 2.2 = gamma corrected)


// Timer interrupt stuff. Big thanks to the tutorial at
// http://www.uchobby.com/index.php/2007/11/24/arduino-interrupts/
unsigned int latency = 0;
unsigned char timerLoadValue;
#define TIMER_CLOCK_FREQ 2000000.0 // from prescaler to timer2 counter
#define TIME_OUT_FREQ 40000.0      // how often the interrupt is invoked.
unsigned int latencySum;
unsigned int sampleCount;


// LED animation stuff
int buttonState       = 1; // Active-LOW button input
int buttonReading     = 1;
int lastButtonReading = 1;
int selFunc    =  0; // selected function
int functions  = 10; // number of functions
int currentAni =  3; // current LED animation
int animations = 21;  // nr. of different LED animations

// some fixed color mixes
const int colorMixes = 8;
RGB colormix[colorMixes] = {
  { 255, 255, 255 },
  { 255,   0,   0 },
  {   0, 255,   0 },
  {   0,   0, 255 },
  { 255, 255,   0 },
  { 255,  63,   0 },
  { 255,   0, 255 },
  {   0, 255, 255 }
};

int colormixnr = 0;
const RGB black = {0, 0, 0};


// FFT stuff
char im[16], data[16];



void setup() {
  // Setting up outputs
  for (int x = 0; x < 18; x++)
  {
    pinMode(x, OUTPUT);
    digitalWrite(x, LOW);
  }
  // A button
  pinMode(buttonPin, INPUT_PULLUP);
  //pinMode(analogPin, INPUT);

  // Analog input stuff
  //ADCSRA = 0xe5; // set the adc to free running mode
  //ADMUX = 0x47; // use adc7
  //DIDR0 = 0x80; // turn off the digital input for adc7
  analogReference(EXTERNAL);


  // PWM brightness correction tests
  for (int i=0; i<256; i++)
  {
    //gamma[i] = i; // no correction
   
    // human perception to linear brightness (or vice versa)
    //gamma[i] = (unsigned char)(255.0 * (i / 255.0) * pow(i / 255.0, 0-gammaVal));

    if (gammaVal == 1.0) gamma[i] = i; // exactly no correction (avoids float rounding)
    else gamma[i] = (unsigned char)(255.0 * pow(i / 255.0, gammaVal));
  }

  // initial white cube
  fillcube((RGB) {
    255, 255, 255
  });

  // Start the timer interrupt
  timerLoadValue = SetupTimer2(TIME_OUT_FREQ);

  startTime = millis();
}



void loop() {
  currentTime   = millis();
  buttonReading = digitalRead(buttonPin);

  // int test = analogRead(analogPin);

  if (buttonReading != lastButtonReading) {
    buttonChangeTime = currentTime;
  }

  // Button debounce
  if ((currentTime - buttonChangeTime) > debounceDelay)
  {
    if (buttonState != buttonReading) {
      buttonState = buttonReading;
      if (buttonState == LOW) {
        startTime = currentTime;
        selFunc++;
        fillcube(black);
        if (selFunc > functions - 1) selFunc = 0;
      }
    }
  }

  //test
  //currentAni = selFunc;

  if (selFunc > 1) {
    switch (selFunc) {
      case 0:
        currentAni = ((currentTime-startTime)/60000)%animations;
        break;
      case 1:
        fftBeatChange();
        break;
      case 2:
        currentAni = 3;
        break;
      case 3:
        currentAni = 5;
        break;
      case 4:
        currentAni = 6;
        break;
      case 5:
        currentAni = 10;
        break;
      case 6:
        currentAni = 12;
        break;
      case 7:
        currentAni = 13;
        break;
      case 8:
        currentAni = 17;
        break;
      case 9:
        currentAni = 20;
        break;
    }
  }


  // simple tests
  switch (currentAni) {
    case 0:
      staticColorCube();
      break;
    case 1:
      spiralRunningLeds();
      break;
    case 2:
      spinningPlane();
      break;
    case 3:
      dnaSpin( 0.75 );
      break;
    case 4:
      simpleColorTest();
      break;
    case 5:
      slowEvenMoodlamp();
      break;
    case 6:
      moodlamp1();
      break;
    case 7:
      moodlamp2();
      break;
    case 8:
      rgbxyzslider2();
      break;
    case 9:
      triangleFader();
      break;
    case 10:
      nPhasePulser(2, 0);
      break;
    case 11:
      nPhasePulser2(7, 0);
      break;
    case 12:
      nPhasePulser2(7, 2);
      break;
    case 13:
      orbits( 0.0 );
      break;
    case 14:
      orbits( 0.96 );
      break;
    case 15:
      adcTest();
      break;
    case 16:
      fftTest1();
      break;
    case 17:
      fftTest2();
      break;
    case 18:
      fftTest3();
      break;
    case 19:
      fftTest4();
      break;
    case 20:
      fire();
      break;
  }
  lastButtonReading = buttonReading;
}



void fillcube( RGB color ) {
  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 3; y++) {
      for (int z = 0; z < 3; z++) {
        cube[x][y][z] = color;
      }
    }
  }
}


int dsign( double value ) {
  if (value < 0) return -1; else return 1;
}


void staticColorCube() {
  RGB color;
  for (int x = 0; x < 3; x++) {
    color.r = (x * 127.5) ;
    for (int y = 0; y < 3; y++) {
      color.g = (y * 127.5);
      for (int z = 0; z < 3; z++) {
        color.b = (z * 127.5);
        cube[x][y][z] = color;
      }
    }
  }
}


// Simple spiralling running LEDs
void spiralRunningLeds() {
  int x;
  int y;
  int z;
  int sign;
  static int ox, oy, oz, oldFill;
  runtime = currentTime; // - startTime;
  int period = (0x01 << 2 - ((runtime / 12000) % 3)) * 250; // successively shorter periods
  int fill = (runtime / 24000) % 2; // single pixel or spiral fill

  int color = (runtime / (period * 3)) % colorMixes; // 3 rounds/color
  double angle = ( (runtime % period) / (double)period) * 2 * PI;

  sign = dsign(cos(angle));
  x = 1 + int( cos(angle) + sign / 2.0 );
  y = (runtime / period) % 3;
  sign = dsign(sin(angle));
  z = 1 + int( sin(angle) + sign / 2.0 );

  if (!fill | !oldFill) cube[ox][oy][oz] = black;
  cube[x][y][z] = colormix[color];
  ox = x;
  oy = y;
  oz = z;
  oldFill = fill;
}

// Simple spinning plane thing
void spinningPlane() {
  int x;
  int y;
  int z;
  int sign;
  static int ox, oz;
  runtime = currentTime; // - startTime;
  int period = 750 ;

  int color1 = (runtime / (period * 2)) % colorMixes;
  int color2 = ((runtime + (period * 4)) / (period * 2)) % colorMixes;
  RGB color3 = colormix[color1] | colormix[color2];
  int fill = (runtime / 10000) % 2; // single pixel or spiral fill
  int spinDir = (runtime / 30000) % 3;

  double angle = (runtime / (double)period) * 2 * PI;
  sign = dsign(cos(angle));
  x = 1 + int( cos(angle) + sign / 2.0 );
  sign = dsign(sin(angle));
  z = 1 + int( sin(angle) + sign / 2.0 );

  switch (spinDir) {
    case 0:  // Y axis spin
      for (int y = 0; y < 3; y++) {
        if (!fill) cube[ox][y][oz] = black;
        if (!fill) cube[2 - ox][y][2 - oz] = black;
        cube[1][y][1] = color3;
        cube[x][y][z] = colormix[color1];
        cube[2 - x][y][2 - z] = colormix[color2];
      }
      break;
    case 1:  // X axis spin
      for (int y = 0; y < 3; y++) {
        if (!fill) cube[y][ox][oz] = black;
        if (!fill) cube[y][2 - ox][2 - oz] = black;
        cube[y][1][1] = color3;
        cube[y][x][z] = colormix[color1];
        cube[y][2 - x][2 - z] = colormix[color2];
      }
      break;
    case 2:  // Z axis spin
      for (int y = 0; y < 3; y++) {
        if (!fill) cube[ox][oz][y] = black;
        if (!fill) cube[2 - ox][2 - oz][y] = black;
        cube[1][1][y] = color3;
        cube[x][z][y] = colormix[color1];
        cube[2 - x][2 - z][y] = colormix[color2];
      }
      break;
  }
  ox = x;
  oz = z;
}


// Twisted spinning thing
void dnaSpin( double decayRate ) {
  int x;
  int y;
  int z;
  int sign;
  static int ox[3], oz[3];
  runtime = currentTime; // - startTime;
  int period = 750 ;

  RGB color;
  long redPeriod;
  long bluePeriod;
  long greenPeriod;
  int fill = (runtime / 10000) % 2; // single pixel or spiral fill

  redPeriod   = 5000;
  greenPeriod = 6000;
  bluePeriod  = 7000;


  for (int i = 0; i < 3; i++) {
    color.r = 127.5 * ( 1 + sin( (double)(2 * PI * ( i / 10.0 + ( currentTime % redPeriod ) / (double)redPeriod))));
    color.g = 127.5 * ( 1 + sin( (double)(2 * PI * ( i / 10.0 + ( currentTime % greenPeriod ) / (double)greenPeriod))));
    color.b = 127.5 * ( 1 + sin( (double)(2 * PI * ( i / 10.0 + ( currentTime % bluePeriod ) / (double)bluePeriod))));

    double angle = (runtime / (double)period) * 2 * PI + (PI * i / 6.0); // angle twist
    sign = dsign(cos(angle));
    x = 1 + int( cos(angle) + sign / 2.0 ); // adjust to range 0-2 and correctly round value
    sign = dsign(sin(angle));
    z = 1 + int( sin(angle) + sign / 2.0 );

    if (!fill) cube[ox[i]][i][oz[i]] = black;
    if (!fill) cube[2 - ox[i]][i][2 - oz[i]] = black;
    cube[1][i][1]     = color;
    cube[x][i][z]     = color;
    cube[2 - x][i][2 - z] = color;
    ox[i] = x;
    oz[i] = z;
  }
}


// Simple color tests
void simpleColorTest() {
  int c = (currentTime / 1000) % colorMixes;
  fillcube(colormix[c]);
}


// Slow color fade
void slowEvenMoodlamp() {
  RGB color;
  long redPeriod;
  long bluePeriod;
  long greenPeriod;

  redPeriod   = 26900;
  greenPeriod = 20000;
  bluePeriod  = 30100;

  color.r = 127.5 * ( 1 + sin( (double)(2 * PI * ( currentTime % redPeriod ) / (double)redPeriod)));
  color.g = 127.5 * ( 1 + sin( (double)(2 * PI * ( currentTime % greenPeriod ) / (double)greenPeriod)));
  color.b = 127.5 * ( 1 + sin( (double)(2 * PI * ( currentTime % bluePeriod ) / (double)bluePeriod)));
  fillcube( color );

}


// XYZ fade avoiding the darker PWM levels
void moodlamp1() {
  RGB color[3];
  long redPeriod;
  long bluePeriod;
  long greenPeriod;

  redPeriod   = 26900;
  greenPeriod = 20000;
  bluePeriod  = 30100;

  for (int i = 0; i < 3; i++) {
    color[i].r = 31 + 112 * ( 1 + sin( (double)(2 * PI * ( ( currentTime + ((long)i * redPeriod / 5) ) % redPeriod ) / (double)redPeriod)));
    color[i].g = 31 + 112 * ( 1 + sin( (double)(2 * PI * ( ( currentTime + ((long)i * greenPeriod / 5) ) % greenPeriod ) / (double)greenPeriod)));
    color[i].b = 31 + 112 * ( 1 + sin( (double)(2 * PI * ( ( currentTime + ((long)i * bluePeriod / 5) ) % bluePeriod ) / (double)bluePeriod)));
  }

  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 3; y++) {
      for (int z = 0; z < 3; z++) {
        cube[x][y][z] = (RGB) {
          color[x].r, color[y].g, color[z].b
        };
      }
    }
  }
}



// XYZ fade full PWM levels
void moodlamp2() {
  RGB color[3];
  long redPeriod;
  long bluePeriod;
  long greenPeriod;

  redPeriod   = 26900;
  greenPeriod = 20000;
  bluePeriod  = 30100;

  for (int i = 0; i < 3; i++) {
    color[i].r = 127.5 * ( 1 + sin( (double)(2 * PI * ( i / 3.0 + ( currentTime % redPeriod ) / (double)redPeriod))));
    color[i].g = 127.5 * ( 1 + sin( (double)(2 * PI * ( i / 3.0 + ( currentTime % greenPeriod ) / (double)greenPeriod))));
    color[i].b = 127.5 * ( 1 + sin( (double)(2 * PI * ( i / 3.0 + ( currentTime % bluePeriod ) / (double)bluePeriod))));
  }

  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 3; y++) {
      for (int z = 0; z < 3; z++) {
        cube[x][y][z] = (RGB) {
          color[x].r, color[y].g, color[z].b
        };
      }
    }
  }
}


// Sliding color axises
void rgbxyzslider2() {
  RGB color[3];
  
  double redPeriod = 3000;
  double greenPeriod = 4000;
  double bluePeriod = 5000;

  runtime = currentTime; // - startTime;

  for (int i = 0; i < 3; i++) {
    color[i].r = colorMax * ((int)(((i + 1) / 4.0) + (runtime / redPeriod)) % 2);
    color[i].g = colorMax * ((int)(((i + 1) / 5.0) + (runtime / greenPeriod)) % 2);
    color[i].b = colorMax * ((int)(((i + 1) / 6.0) + (runtime / bluePeriod)) % 2);
  }


  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 3; y++) {
      for (int z = 0; z < 3; z++) {
        cube[x][y][z] = (RGB) { color[x].r, color[y].g, color[z].b };
      }
    }
  }
}



void PWMlevelTest( unsigned char val ) {
  fillcube( {val, val, val} );
}



void triangleFader() {
  RGB color;
  int period = 8192/256;
  runtime = currentTime - startTime;

  //double val = 127.5 * ( 1 - cos( 2 * PI * ((double)runtime / period) ) );
  int ci = (runtime/(period*256*2))%colorMixes; //new color every other fade
  int val = (runtime/period) % 256;
  int dir = (runtime/(period*256))%2; // alternate ramp up/down
  if (dir == 1) val = 255-val;
  
  color = (val/255.0) * colormix[ci];

  fillcube( color );
}



// n-phase pulsating lights
// n <= colorMixes, p>0 --> variable periods and color periods
void nPhasePulser(int n, int p) {
  RGB color[n], col;
  runtime = currentTime; // - startTime;
  int ci;
  double val;
  int period = 2000;
  
  // successively shorter periods
  if (p > 0) period = (0x01 << 2 - ((runtime / 12000) % 3)) * 1000;

  for (int w=0; w<n; w++){
    if (p>0)  ci = ((runtime + (period/4) + (w*period/n )) / (period*(w*p+2))) % colorMixes;
    if (p==0) ci = ((runtime + (period/4) + (w*period/n )) / (period*1)) % colorMixes;
    ci += w;
    ci  = ci%colorMixes;
    col = colormix[ci];
    val = 127.5 * (1 + sin(2*PI* ( ((runtime+(period*w/n))%period) / (double)period)));
    color[w] = (val/255.0) * col;
  }
   
  for( int x=0; x<3; x++) {
    for (int y=0; y<3; y++) {
      for (int z=0; z<3; z++) {
        for (int w=0; w<n; w++) {
          if ((x+y+z)%n == w) {
            cube[x][y][z] = color[w];
            continue;
          }
        }
      }
    }
  }
}




// n-phase pulsating lights, overlapping colors a bit
// n <= colorMixes, p>0 --> variant 2
void nPhasePulser2(int n, int p) {
  RGB color[n], colnow, colnxt;
  runtime = currentTime; // - startTime;
  int ci, cn; // color index, next color index
  double val1, val2;
  //int period = 2000;

  // successively shorter periods
  int period = (0x01 << 2 - ((runtime / 12000) % 3)) * 500;

  for (int w=0; w<n; w++){
    ci = ((runtime + (period/4) + (w*period/n )) / period) % colorMixes;
    cn = ((runtime + (period/4) + (w*period/n) + (period/2)) / period) % colorMixes;
    colnow = colormix[ci];
    colnxt = colormix[cn];
    val1 = 127.5 * (1 + sin(2*PI* ( ((runtime+(period*w/n))%period) / (double)period)));
    if (p==0) {
      val2 = 127.5 * (1 + sin(2*PI* ( ((runtime+(period*w/n)+(period/2))%period) / (double)period)));
      colnxt = (val2/255.0) * colnxt;
    }
    color[w] = colnow | colnxt;
    color[w] = (val1/255.0) * color[w];
  }
   
  for( int x=0; x<3; x++) {
    for (int y=0; y<3; y++) {
      for (int z=0; z<3; z++) {
        for (int w=0; w<n; w++) {
          if ((x+y+z)%n == w) {
            cube[x][y][z] = color[w];
            continue;
          }
        }
      }
    }
  }
}



// Simple spiralling running LEDs
void orbits( double decayRate ) {
  int cx, cy, cz;
  double x, y, z;
  int signx, signy, signz;
  static int ox, oy, oz, oldFill;
  runtime = currentTime; // - startTime;

  RGB color;
  long redPeriod = 20000;
  long greenPeriod = 25000;
  long bluePeriod = 30000;

  color.r = 127.5 * ( 1 + sin( (double)(2 * PI * ( currentTime % redPeriod ) / (double)redPeriod)));
  color.g = 127.5 * ( 1 + sin( (double)(2 * PI * ( currentTime % greenPeriod ) / (double)greenPeriod)));
  color.b = 127.5 * ( 1 + sin( (double)(2 * PI * ( currentTime % bluePeriod ) / (double)bluePeriod)));

  //int period     = 500;
  //int elevPeriod = 10000;
  int period = (0x01 << 2 - ((runtime / 12000) % 3)) * 250; // successively shorter periods
  int elevPeriod = (0x01 << 7 - ((runtime / 30000) % 7)) * 300; // successively shorter periods

  //int color = (runtime / (period * 3)) % colorMixes; // 3 rounds/color

  double angle     = ( (runtime % period) / (double)period) * 2 * PI;
  double elevAngle = ( (runtime % elevPeriod) / (double)elevPeriod) * 2 * PI;
  
  x = 1.6 * cos(angle);
  z = 1.6 * sin(angle);
  y = x * sin(elevAngle);
  x = x * cos(elevAngle);
  
  signx = dsign(x);
  signy = dsign(y);
  signz = dsign(z);

  cx = 1 + (int) (x + signx/2.0);
  cy = 1 + (int) (y + signy/2.0);
  cz = 1 + (int) (z + signz/2.0);

  if (cx<0) cx = 0;
  if (cx>2) cx = 2;
  if (cy<0) cy = 0;
  if (cy>2) cy = 2;
  if (cz<0) cz = 0;
  if (cz>2) cz = 2;

  if (decayRate <= 0) {
    cube[ox][oy][oz] = black; // erase last dot
  } else {
    for (int i=0; i<3; i++) {
      for (int j=0; j<3; j++) {
        cube[i][j][0] = decayRate * cube[i][j][0];
        cube[i][j][2] = decayRate * cube[i][j][2];
        if (i==1 && j==1) continue;
        cube[i][j][1] = decayRate * cube[i][j][1];
      }
    }
  } // fade trail
  
  cube[cx][cy][cz] = !color;
  cube[1][1][1] = color;
  ox = cx;
  oy = cy;
  oz = cz;
}


// Simple VU meter test
void adcTest() {
  int k = analogRead(audioPin);

  for (int i = 0; i < 3; i++) {
    cube[0][i][0] = VUintensity( k );
    k = k - 342;
  }
}



// Static FFT test first 3 columns only
void fftTest1() {
  //noInterrupts();
  for (int i=0; i<16; i++)
  {
    int val = analogRead(audioPin);
    data[i] = val/4 - 128;
    im[i] = 0;
    //delayMicroseconds(200);
  }
  //interrupts();

  fix_fft(data, im, 4, 0);

  int bass, midtone, treeble;

  // Disregarding some bands
  for (int i = 0; i < 3; i++) {
    data[i] = sqrt(data[i] * data[i] + im[i] * im[i]);
  }

  bass    = data[0] + data[0] + data[0] + data[0];
  midtone = data[1] + data[1] + data[1] + data[1];
  treeble = data[2] + data[2] + data[2] + data[2];

  for (int i=0; i<3; i++) {    
    // display on cube somehow
    cube[0][i][0] = VUintensity( bass );
    bass = bass - 64;
    cube[1][i][0] = VUintensity( midtone );
    midtone = midtone - 64;
    cube[2][i][0] = VUintensity( treeble );
    treeble = treeble - 64;
  }
}



RGB VUintensity( int value ) {
  RGB output = black;
  const int waveLRed = 512;
  const int phaseRed = 384;
  const int waveLGrn = 192;
  const int phaseGrn = 96;
  const int waveLBlu = 128;
  const int phaseBlu = 96;

  if (value<0) value = 0;
  if (value>255) value = 255;

  output.r = (unsigned char) (127.5 + 127.5 * sin( TWO_PI * (((double)value/waveLRed) + (double)phaseRed/waveLRed)));
  
  if (value<48 || value>240) {
    output.g = 0;
  } else {
    output.g = (unsigned char) (127.5 + 127.5 * sin( TWO_PI * (((double)value/waveLGrn) + (double)phaseGrn/waveLGrn)));
  }
  
  if (value>128) {
    output.b = 0;
  } else {
    output.b = (unsigned char) (127.5 + 127.5 * sin( TWO_PI * (((double)value/waveLBlu) + (double)phaseBlu/waveLBlu)));
  }

  return output;
}



// 3D moving spectrogram test
void fftTest2() {
  const int slotDelay = 50;
  static unsigned long timeSlot = currentTime / slotDelay;

  if (timeSlot >= (currentTime / slotDelay)) return;
  timeSlot = currentTime / slotDelay;
  
  for (int i=0; i<16; i++)
  {
    int val = analogRead(audioPin);
    data[i] = val/4 - 128;
    im[i] = 0;
  }
  fix_fft(data, im, 4, 0);

  int bass, midtone, treeble;
  // Disregarding some frequency bands
  for (int i = 1; i < 8; i++) {
    data[i] = sqrt(data[i] * data[i] + im[i] * im[i]);
  }
  bass    = data[0] + data[0] + data[1] + data[1];
  midtone = data[2] + data[2] + data[3] + data[3];
  treeble = data[4] + data[5] + data[6] + data[7];

  // move spectrogram 
  for (int z=2; z>0; z--){
    for (int y=0; y<3; y++){
      cube[0][y][z] = cube[0][y][z-1];
      cube[1][y][z] = cube[1][y][z-1];
      cube[2][y][z] = cube[2][y][z-1];
    }
  }

  // display on cube somehow
  for (int i=0; i<3; i++) {
    cube[0][i][0] = VUintensity( bass );
    bass = bass - 32;
    cube[1][i][0] = VUintensity( midtone );
    midtone = midtone - 32;
    cube[2][i][0] = VUintensity( treeble );
    treeble = treeble - 32;
  }

}



// 3D moving spectrogram test #2
void fftTest3() {
  const int slotDelay = 10;
  static unsigned long timeSlot = currentTime / slotDelay;

  if (timeSlot >= (currentTime / slotDelay)) return;
  timeSlot = currentTime / slotDelay;
  
  // simple ASAP (ish) sampling (10-ish KHz on 16MHz Atemega)
  //noInterrupts();
  for (int i=0; i<16; i++)
  {
    int val = analogRead(audioPin);
    data[i] = val/4 - 128;
    im[i] = 0;
  }
  //interrupts();
  fix_fft(data, im, 4, 0);

  int bass, midtone, treeble;
  // Disregarding some frequency bands
  for (int i = 1; i < 8; i++) {
    data[i] = sqrt(data[i] * data[i] + im[i] * im[i]);
  }

  bass    = data[0] * 4;
  midtone = data[1] * 4;
  treeble = data[2] * 4; // ish

  // move spectrogram 
  for (int z=2; z>0; z--){
    for (int y=0; y<3; y++){
      cube[0][y][z] = cube[0][y][z-1];
      cube[1][y][z] = cube[1][y][z-1];
      cube[2][y][z] = cube[2][y][z-1];
    }
  }

  // display on cube somehow
  for (int i=0; i<3; i++) {
    cube[0][i][0] = VUintensity( bass );
    bass = bass - 32;
    cube[1][i][0] = VUintensity( midtone );
    midtone = midtone - 32;
    cube[2][i][0] = VUintensity( treeble );
    treeble = treeble - 32;
  }

}



// Spectrogram wall, each column own frequency band
void fftTest4() {

  // Mapping frequency bins around the cube walls
  static xzmap remap[8] = {
    {0,0},
    {1,0},
    {2,0},
    {2,1},
    {2,2},
    {1,2},
    {0,2},
    {0,1}
  };

  const int slotDelay = 50;
  static unsigned long timeSlot = currentTime / slotDelay;

  if (timeSlot >= (currentTime / slotDelay)) return;
  timeSlot = currentTime / slotDelay;

  // Sampling ASAP
  //noInterrupts();
  for (int i=0; i<16; i++)
  {
    int val = analogRead(audioPin);
    data[i] = val/4 - 128;
    im[i] = 0;
  }
  //interrupts();
  fix_fft(data, im, 4, 0);

  for (int i = 0; i < 8; i++) {
    data[i] = sqrt(data[i] * data[i] + im[i] * im[i]);
  }

  // display on cube somehow
  for (int b=0; b<8; b++){
    int x = remap[b].x;
    int z = remap[b].z;
    for (int i=0; i<3; i++) {
      cube[x][i][z] = VUintensity( data[b] * 4 );
      data[b] = data[b] - 32;
    }
  }
}


// Beat changes animation - test
void fftBeatChange() {
  //noInterrupts();
  for (int i=0; i<16; i++)
  {
    int val = analogRead(audioPin);
    data[i] = val/4 - 128;
    im[i] = 0;
    //delayMicroseconds(200);
  }
  //interrupts();

  fix_fft(data, im, 4, 0);

  int bass, midtone, treeble;

  for (int i = 0; i < 1; i++) {
    data[i] = sqrt(data[i] * data[i] + im[i] * im[i]);
  }

  bass    = data[0] * 4;

  if (bass > 230) {
    currentAni++;
    startTime = currentTime;
    fillcube(black);
  }

  if (currentAni > animations) currentAni = 0;

}
