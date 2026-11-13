// Timer interrupt stuff. Big thanks to the tutorial at
// http://www.uchobby.com/index.php/2007/11/24/arduino-interrupts/



//Setup Timer2.
//Configures the ATMega168 / ATmega328 8-Bit Timer2 to generate an interrupt at the specified frequency.
//Returns the time load value which must be loaded into TCNT2 inside your ISR routine.
unsigned char SetupTimer2(float timeoutFrequency)
{
  unsigned char result; //The value to load into the timer to control the timeout interval.

  //Calculate the timer load value
  result = (int)((257.0 - (TIMER_CLOCK_FREQ / timeoutFrequency)) + 0.5); //the 0.5 is for rounding;

  //Timer2 Settings:
  //Timer clock = 16MHz/prescaler
  TCCR2A = 0;
  TCCR2B = 0 << CS22 | 1 << CS21 | 0 << CS20; // prescaler settings to divide by 8
  //TCCR2B = B00000010;

  //Timer2 Overflow Interrupt Enable
  TIMSK2 = 1 << TOIE2;

  //load the timer for its first cycle
  TCNT2 = result;
  return (result);
}



ISR(TIMER2_OVF_vect)
{
  static unsigned char PWMcycle = 0;
  static int rowCounter = 0;

  unsigned char portDout;

  unsigned char r1, r2, r3;
  unsigned char g1, g2, g3;
  unsigned char b1, b2, b3;

  int y;
  int z;

  if (PWMcycle >= colorMax)
  {
    //PWMcycle = colorStep;
    PWMcycle = 0;
    rowCounter++ ;
    if (rowCounter > 8) rowCounter = 0;

    // Turn off all outputs
    PORTD = 0x00;       // Clear column bits
    PORTB &= B11111110; // clear port B last column bit
    PORTB |= B00111110; // Set port B rows. Next-but lowest 5 bits (rows 1-5)
    PORTC |= B11111111; // Set port C lowest 4 bits (rows 6-9)

    // Active-low rows
    switch (rowCounter) {
      case 0:
        PORTB &= B11111101;
        break;
      case 1:
        PORTB &= B11111011;
        break;
      case 2:
        PORTB &= B11110111;
        break;
      case 3:
        PORTB &= B11101111;
        break;
      case 4:
        PORTB &= B11011111;
        break;
      case 5:
        PORTC &= B11111110;
        break;
      case 6:
        PORTC &= B11111101;
        break;
      case 7:
        PORTC &= B11111011;
        break;
      case 8:
        PORTC &= B11110111;
    }
  }

  // build PWM column data (one row) for one "PWM step"
  // Probable issue if 256 PWM levels (use >= but not include 0/zero ?)
  y = gety[rowCounter];
  z = getz[rowCounter];

  r1 = gamma[cube[0][y][z].r];
  r2 = gamma[cube[1][y][z].r];
  r3 = gamma[cube[2][y][z].r];
  g1 = gamma[cube[0][y][z].g];
  g2 = gamma[cube[1][y][z].g];
  g3 = gamma[cube[2][y][z].g];
  b1 = gamma[cube[0][y][z].b];
  b2 = gamma[cube[1][y][z].b];
  b3 = gamma[cube[2][y][z].b];

  do {
    portDout = 0;
    PORTB &= B11111110;

    // RED bits
    if (r1 > PWMcycle) portDout |= B00000100;
    if (r2 > PWMcycle) portDout |= B00100000;
    if (r3 > PWMcycle) PORTB |= B00000001; // port B red bit directly

    // GREEN bits
    if (g1 > PWMcycle) portDout |= B00000010;
    if (g2 > PWMcycle) portDout |= B00010000;
    if (g3 > PWMcycle) portDout |= B10000000;

    // BLUE bits
    if (b1 > PWMcycle) portDout |= B00000001;
    if (b2 > PWMcycle) portDout |= B00001000;
    if (b3 > PWMcycle) portDout |= B01000000;

    // Display the rest of the RGB row bits
    PORTD = portDout;

    // update where we are in the PWM cycle
    PWMcycle += colorStep;

    // Stretch out the last PWM levels to a smooth-ish transfer to the normal interrupt-generated ones
    if (PWMcycle > 80 && PWMcycle < 128) {
      for (int i = 80; i < PWMcycle; i++) {
        asm("nop");
      }
      if (PWMcycle > 110) {
        for (int i = 110; i < PWMcycle; i++) {
          asm("nop");
        }
      }
    }
  } while (PWMcycle < 128); // Run the first part of PWM in one go (darker lower levels + faster)

  if (PWMcycle > 127) {
    //Capture the current timer value. This is how much error we
    //have due to interrupt latency and the work in this function
    latency = TCNT2;
  }
  //Reload the timer and correct for latency. Added since it counts up to overflow (less counting needed).
  TCNT2 = latency + timerLoadValue;
  //TCNT2=timerLoadValue;

}
