/*
  wiring.c - Partial implementation of the Wiring API for the ATmega8.
  Part of Arduino - http://www.arduino.cc/

  Copyright (c) 2005-2006 David A. Mellis

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA

  Updated for 'xmega' core by bob frazier, S.F.T. Inc. - http://mrp3.com/

  In some cases, the xmega updates make assumptions about the pin assignments.
  See 'pins_arduino.h' for more detail.

*/

#include "wiring_private.h"


// The xmega architecture differs significantly from the mega in a number
// of ways that render the existing code unworkable.  Therefore a complete
// re-write was done to ensure 100% compatibility at the code level.



// the prescaler is set so that timer0 ticks every 64 clock cycles, and the
// the overflow handler is called every 256 ticks.
#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64 * 256))

// the whole number of milliseconds per timer0 overflow
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW / 1000)

// NOTE:  xmega runs at 32 mhz typically.  However, it uses THE PERIPHERAL CLOCK
//        for the timer.  Normally this is the same as the CPU clock unless you use
//        some crazy clock pre-scaler.
//        See sections 6.9 and 6.10 in D manual for system clock setup

// the fractional number of milliseconds per timer0 overflow. we shift right
// by three to fit these numbers into a byte. (for the clock speeds we care
// about - 8 and 16 MHz and 32 Mhz for xmega - this doesn't lose precision.)

#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW % 1000) >> 3)
#define FRACT_MAX (1000 >> 3)

volatile unsigned long timer0_overflow_count = 0;
volatile unsigned long timer0_millis = 0;
static unsigned char timer0_fract = 0;

// timer zero overflow - affects pins 5 and 6 for PWM on Arduino and compatibles
// what I want to do is simulate what the Arduino already does, using TCD2
// since it will share the same pre-scaler AND clock for all of the port D PWM out

// the timer prescaler MUST tick every 64 clock cycles for this to work, just like the mega TIMER 0
// NOTE:  at 32Mhz MICROSECONDS_PER_TIMER0_OVERFLOW will be 512 - consider a divider of 128 instead
//        unless you want 2khz for the PWM (which is actually a good idea, for servos etc.)

ISR(TCD2_LUNF_vect) /* for this to work the limit must be 255 */
{
  // copy these to local variables so they can be stored in registers
  // (volatile variables must be read from memory on every access)
  unsigned long m = timer0_millis;
  unsigned char f = timer0_fract;

#if MILLIS_INC > 0
  m += MILLIS_INC;
#endif // MILLIS_INC
  f += FRACT_INC;
  if (f >= FRACT_MAX)
  {
    f -= FRACT_MAX;
    m += 1;
  }

  timer0_fract = f;
  timer0_millis = m;
  timer0_overflow_count++;
}

unsigned long millis()
{
  unsigned long m;
  uint8_t oldSREG = SREG;

  // disable interrupts while we read timer0_millis or we might get an
  // inconsistent value (e.g. in the middle of a write to timer0_millis)
  cli();
  m = timer0_millis;
  SREG = oldSREG;

  return m;
}

unsigned long micros()
{
  unsigned long m;
  uint8_t t, oldSREG;

  oldSREG = SREG;
  cli(); // for consistency, don't let this part get interrupted

  m = timer0_overflow_count; // for xmega it's really an underflow
  t = 255 - TCD2_LCNT; // 'low' count, it's what we interrupt on (and it always counts DOWN)
                       // must subtract count value from 255 for this to work correctly

  // check the interrupt flag to see if I just got an underflow
  if((TCD2_INTFLAGS & _BV(0)) && (t < 255)) // which means I underflowed but didn't call the ISR yet
  {
    m++; // increment ISR count for more accurate microseconds
  }

  SREG = oldSREG;

  return ((m << 8) + t) * (64 / clockCyclesPerMicrosecond()); // TODO:  make the '64' a #define ?
}

void delay(unsigned long ms)
{
  uint16_t start = (uint16_t)micros();

  while (ms > 0) /* BF - fixed K&R style to Allman for readability/consistency */
  {
    if (((uint16_t)micros() - start) >= 1000)
    {
      ms--;
      start += 1000;
    }
  }
}

/* Delay for the given number of microseconds.  Assumes a 8 or 16 MHz clock. */
// NOTE:  for XMEGA, you can have a 32mhz clock
void delayMicroseconds(unsigned int us)
{
  // calling avrlib's delay_us() function with low values (e.g. 1 or
  // 2 microseconds) gives delays longer than desired.
  //delay_us(us);
#if F_CPU >= 32000000L /* the xmega typically has this */

  // for a one-microsecond delay, simply wait 12 cycles and return. The overhead
  // of the function call yields a delay of exactly a one microsecond.
  __asm__ __volatile__ (
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop" "\n\t"
    "nop"); //just waiting 12 cycle
  if (--us == 0)
    return;

  // the following loop takes a 1/8 of a microsecond (4 cycles)
  // per iteration, so execute it five times for each microsecond of
  // delay requested.
  us = (us<<3);// * 8

  // account for the time taken in the preceeding commands.
  us -= 2;

#elif F_CPU >= 20000000L
  // for the 20 MHz clock on rare Arduino boards

  // for a one-microsecond delay, simply wait 2 cycle and return. The overhead
  // of the function call yields a delay of exactly a one microsecond.
  __asm__ __volatile__ (
    "nop" "\n\t"
    "nop"); //just waiting 2 cycle
  if (--us == 0)
    return;

  // the following loop takes a 1/5 of a microsecond (4 cycles)
  // per iteration, so execute it five times for each microsecond of
  // delay requested.
  us = (us<<2) + us; // x5 us

  // account for the time taken in the preceeding commands.
  us -= 2;

#elif F_CPU >= 16000000L
  // for the 16 MHz clock on most Arduino boards

  // for a one-microsecond delay, simply return.  the overhead
  // of the function call yields a delay of approximately 1 1/8 us.
  if (--us == 0)
    return;

  // the following loop takes a quarter of a microsecond (4 cycles)
  // per iteration, so execute it four times for each microsecond of
  // delay requested.
  us <<= 2;

  // account for the time taken in the preceeding commands.
  us -= 2;
#else
  // for the 8 MHz internal clock on the ATmega168

  // for a one- or two-microsecond delay, simply return.  the overhead of
  // the function calls takes more than two microseconds.  can't just
  // subtract two, since us is unsigned; we'd overflow.
  if (--us == 0)
    return;
  if (--us == 0)
    return;

  // the following loop takes half of a microsecond (4 cycles)
  // per iteration, so execute it twice for each microsecond of
  // delay requested.
  us <<= 1;

  // partially compensate for the time taken by the preceeding commands.
  // we can't subtract any more than this or we'd overflow w/ small delays.
  us--;
#endif

  // busy wait
  __asm__ __volatile__ (
    "1: sbiw %0,1" "\n\t" // 2 cycles
    "brne 1b" : "=w" (us) : "0" (us) // 2 cycles
  );
}


// this function is separate since it provides a specific functionality
// and aids in readability by separating it from the main 'init()' code
// regardless of the extra bytes needed to make the function call
void clock_setup(void)
{
unsigned short sCtr;
register unsigned char c1;

  // TODO:  get rid of magic bit numbers, and use bit value constants from iox64d4.h

  // enable BOTH the 32Mhz and 32.768KHz internal clocks [ignore what else might be set for now]

  OSC_CTRL |= _BV(2) | _BV(1); // sect 6.10.1 - enable 32.768KHz (2), 32Mhz (1).  2Mhz is _BV(0)
                               //               _BV(3) and _BV(4) are for PLL and external - will set to 0 later

  if(!(CLK_LOCK & 1)) // clock lock bit NOT set, so I can muck with the clock
  {
    if((CLK_CTRL & 0x7) != 1) // it's not already 32 Mhz
    {
      // wait until 32mhz clock is 'stable'

      for(sCtr=32767; sCtr > 0; sCtr--) // TODO:  remove counter?
      {
        // spin on oscillator status bit for 32Mhz oscillator

        if(OSC_STATUS & _BV(1)) // 32Mhz oscillator is 'ready' (6.10.2)
        {
          break;
        }
      }

      // for now, I can allow the clock to NOT be changed if it's
      // not ready.  This prevents infinite loop inside startup code

      if(!(OSC_STATUS & _BV(1))) // is my oscillator 'ready' ?
      {
        return; // exit - don't change anything
      }

      // switch to 32Mhz clock using internal source

      CCP = CCP_IOREG_gc; // 0xd8 - see D manual, sect 3.14.1 (protected I/O)
      CLK_CTRL = 1; // set the clock to 32Mhz (6.9.1)
    }

    if(CLK_PSCTRL != 0)
    {
      CCP = CCP_IOREG_gc; // 0xd8 - see D manual, sect 3.14.1 (protected I/O)
      CLK_PSCTRL = 0; // set the clock divider(s) to 1:1 (6.9.2)
    }


    // now that I've changed the clock, disable 2Mhz, PLL, and external clocks
    // 32.768KHz should remain active, but I need to make sure it's stable
    OSC_CTRL &= ~(_BV(4) | _BV(3) | _BV(0)); // sect 6.10.1 - disable PLL, external, 2Mhz clocks

    // wait until 32.768KHz clock is 'stable'.  this one goes for a while
    // in case it doesn't stabilize in a reasonable time.  I figure about
    // 64*255 clock cycles should be enough, ya think?
    for(sCtr=65535; sCtr > 0; sCtr--)
    {
      for(c1=255; c1 > 0; c1--)
      {
        if(OSC_STATUS & _BV(2)) // 32.768KHz oscillator is 'ready' (6.10.2)
        {
          sCtr = 1; // this will bail out of the outer loop
          break;
        }
      }
    }

    // enable DFLL auto-calibration of the 32Mhz internal oscillator
    // (it uses the reasonably precise 32.768KHz clock to do it)

    OSC_DFLLCTRL = 0; // sect 6.10.7 - select 32.768KHz osc for everything, basically
    DFLLRC32M_CTRL = 1; // set the bit to enable DFLL calibration - section 6.11.1
  }

  // I'll be using the 1.024khz clock (from the 32.768KHz clock) for the real-time counter
  // this will give me a reasonable "about 1 millisecond" accuracy on the RTC

  // NOTE:  I may not have checked for this if I skipped the previous section,
  //        so now I check again, just in case, to make sure the 32.768KHz osc is stable
  for(sCtr=65535; sCtr > 0; sCtr--)
  {
    for(c1=255; c1 > 0; c1--)
    {
      if(OSC_STATUS & _BV(2)) // 32.768KHz oscillator is 'ready' (6.10.2)
      {
        sCtr = 1; // this will bail out of the outer loop
        break;
      }
    }
  }

  if(!(OSC_STATUS & _BV(2))) // is my oscillator 'ready' ?
  {
    return; // exit - don't change anything else
  }


  // RUN-TIME clock - use internal 1.024 khz source.  32khz needed for this
  CLK_RTCCTRL = 2; // section 6.9.4
}


// this was obtained from a message board.  The function is public to make it easy to
// use the 'Production Signature Row'.  There is a unique identifier for the CPU as well as
// calibration data for the ADC available.  See sect. 4.14 "Production Signature Row"
uint8_t readCalibrationData(uint16_t iIndex)
{
  uint8_t rVal;

  /* Load the NVM Command register to read the calibration row. */
  NVM_CMD = NVM_CMD_READ_CALIB_ROW_gc;

//  rVal = pgm_read_byte_near(iIndex); // this should work correctly
  __asm__ ("lpm %0, Z\n" : "=r" (rVal) : "z" (iIndex));

  /* Clean up NVM Command register. */
  NVM_CMD = NVM_CMD_NO_OPERATION_gc;

  return(rVal);
}

// NOTE:  calibration data for ADC must be loaded BEFORE it's initialized
// ADCA.CALL = readCalibrationData(&PRODSIGNATURES_ADCACAL0);
// ADCA.CALH = readCalibrationData(&PRODSIGNATURES_ADCACAL1);

void init()
{


  cli(); // do this before _ANYTHING_

  // ----------------------------------------------------------
  // first thing first - the system clock _MUST_ run at 32Mhz
  // ----------------------------------------------------------

  clock_setup();


  // The watchdog timer MUST be off (the bootloader should do this too)
  // this next section of code will disable it.

  CCP = CCP_IOREG_gc; // 0xd8 - see D manual, sect 3.14.1 (protected I/O)
  WDT_CTRL = 1; // sets watchdog timer "enable" bit to zero - bit 0 must be set to change bit 1 - section 9.7.1
  CCP = CCP_IOREG_gc; // 0xd8 - see D manual, sect 3.14.1 (protected I/O)
  WDT_WINCTRL = 1; // sets watchdog 'window' timer "enable" bit to zero - bit 0 must be set to change bit 1 - section 9.7.2

  NVM_INTCTRL = 0; // disable interrupts in the NVM subsystem


  // -------------------
  // TIMER CONFIGURATION
  // -------------------


  // set up timers TCC2 and TCD2 and TCE0.  Use pre-scale of 64.  For 32Mhz
  // clock this will run them at 2Khz.  For PWM out, use the comparison
  // result to drive the appropriate pins.

  // If you don't need PWM, or want 'other than 2khz', you can re-configure TCC0/2 and TCE0/2
  // but leave TCD2 alone because it's needed for the system clock (via TCD2_LUNF_vect)

  // TCD2
  // first the clock selection
  TCD2_CTRLA = 5; // b0101 - divide by 64 - D manual 13.9.1
  TCD2_CTRLB = 0; // compare outputs disabled on all 8 bits (13.9.2)
//  TCD2_CTRLC = 0; // when timer not running, sets compare (13.9.3)
  TCD2_CTRLE = 0x2; // b10 - 'split' mode - D manual 13.9.4
  TCD2_CTRLF = 0;   // not resetting or anything (13.9.7)

  TCD2_LPER = 255; // count 255 to 0 (total period = 256)
  TCD2_HPER = 255;

  // pre-assign comparison registers to 'zero' (for PWM out) which is actually 255
  // 'timer 2' counts DOWN.

  TCD2_LCMPA = 255;
  TCD2_LCMPB = 255;
  TCD2_LCMPC = 255;
  TCD2_LCMPD = 255;

  TCD2_HCMPA = 255;
  TCD2_HCMPB = 255;
  TCD2_HCMPC = 255;
  TCD2_HCMPD = 255;

  // enable the underflow interrupt on A, disable on B, disable comparison interrupts
  TCD2_INTCTRLA = 0x3; // enable LOW underflow interrupt, pri level 3 (see 13.9.5 in D manual)
  TCD2_INTCTRLB = 0;   // no comparison or underflow interrupts on anything else


  // TCC2
  // first the clock selection
  TCC2_CTRLA = 5; // b0101 - divide by 64 - D manual 13.9.1
  TCC2_CTRLB = 0; // compare outputs disabled on all 8 bits (13.9.2)
//  TCC2_CTRLC = 0; // when timer not running, sets compare (13.9.3)
  TCC2_CTRLE = 0x2; // b10 - 'split' mode - D manual 13.9.4
  TCC2_CTRLF = 0;   // not resetting or anything (13.9.7)

  TCC2_LPER = 255; // count 255 to 0 (total period = 256)
  TCC2_HPER = 255;

  // pre-assign comparison registers to 'zero' (for PWM out) which is actually 255
  // 'timer 2' counts DOWN.  This, however, would generate a '1' output.

  TCC2_LCMPA = 255;
  TCC2_LCMPB = 255;
  TCC2_LCMPC = 255;
  TCC2_LCMPD = 255;

  TCC2_HCMPA = 255;
  TCC2_HCMPB = 255;
  TCC2_HCMPC = 255;
  TCC2_HCMPD = 255;

  // disable underflow and comparison interrupts
  TCC2_INTCTRLA = 0;   // no underflow interrupts
  TCC2_INTCTRLB = 0;   // no comparison interrupts


  // now set up TCE0 as an 8-bit timer so it's compatible with Arduino's PWM
  // first the clock selection
  TCE0_CTRLA = 5; // b0101 - divide by 64 - D manual 12.11.1
  TCE0_CTRLB = TC_WGMODE_SS_gc; // single-slope PWM.  NOTE:  this counts UP, whereas the other timers count DOWN
               // other bits (high nybble) are OFF - they enable output on the 4 port E pins
//  TCE0_CTRLC = 0; // when timer not running, sets compare (12.11.3)
  TCE0_CTRLD = 0; // not an event timer, 16-bit mode (12.11.4)
  TCE0_CTRLE = 1; // normal 8-bit timer (set to 0 for 16-bit mode) (12.11.5)

  // disable under/overflow and comparison interrupts
  TCE0_INTCTRLA = 0;   // no underflow interrupts
  TCE0_INTCTRLB = 0;   // no comparison interrupts

  // make sure the timer E 'period' register is correctly set at 255 (i.e. 0-255 or 256 clock cycles).
  TCE0_PER = 255;

  // pre-assign comparison registers to 'zero' (for PWM out) which is actually 255
  // timer 0 can be configured to count UP or DOWN, but for single-slope PWM it is
  // always 'UP'.  A value of '255' should generate a '1' output for each PWM.

  TCE0_CCA = 255;
  TCE0_CCB = 255;
  TCE0_CCC = 255;
  TCE0_CCD = 255;


  // in case the bootloader enabled serial or TWI, disable it
  // and make sure the associated port input pins are inputs
  // NOTE:  Port R pins 0 and 1 will be outputs, but all others should be inputs
  //        PR0 and PR1 are designated LED output pins for this design.  PR1 is
  //        the blinking LED pin used by the bootloader.  These will NOT be re-assigned
  //        at this time, but left 'as-is'.

  // -----------------------------------------
  // DISABLE TWI (specifically TWI interrupts)
  // -----------------------------------------

  TWIC_MASTER_CTRLA = 0;
  TWIE_MASTER_CTRLA = 0;
  TWIC_SLAVE_CTRLA = 0;
  TWIE_SLAVE_CTRLA = 0;

  // --------------------
  // DISABLE SERIAL PORTS
  // --------------------

  USARTD0_CTRLA = 0; // disables interrupts
  USARTD0_CTRLB = 0; // disables TX and RX pin override
  USARTC0_CTRLA = 0; // do the same thing
  USARTC0_CTRLB = 0; // for both port C and D

  //-----------------------------
  // PORTS C, D, and E are inputs
  //-----------------------------

  PORTC_DIR = 0; // all 'port C' pins are now inputs
  PORTD_DIR = 0; // all 'port D' pins are now inputs
  PORTE_DIR = 0; // all 'port E' pins are now inputs


  // Added code to pre-set input pins also - note PIN0CTRL through PIN7CTRL are like an array
  // also, 'PORT_ISC_BOTHEDGES_gc | PORT_OPC_TOTEM_gc' evaluates to '0' and is the normal default
  memset(&(PORTC.PIN0CTRL), PORT_ISC_BOTHEDGES_gc | PORT_OPC_TOTEM_gc, 8);
  memset(&(PORTD.PIN0CTRL), PORT_ISC_BOTHEDGES_gc | PORT_OPC_TOTEM_gc, 8);
  memset(&(PORTE.PIN0CTRL), PORT_ISC_BOTHEDGES_gc | PORT_OPC_TOTEM_gc, 4);

  // ---------------------------------------------------
  // ANALOG INPUT PINS - 'INPUT_DISABLED' (recommended)
  // ---------------------------------------------------

  PORTA_DIR = 0; // direction bits - set all of them as input
  PORTB_DIR = 0;

#if 1
  // all analog pins set up for 'INPUT_DISABLED' which is recommended for analog read
  memset(&(PORTA.PIN0CTRL), PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc, 8);
  memset(&(PORTB.PIN0CTRL), PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc, 4);
#else // 1
  // older code (for reference, modified to use constants and not '_BV(2) | _BV(1) | _BV(0)'
  PORTA_PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTA_PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;

  PORTB_PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTB_PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTB_PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
  PORTB_PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc | PORT_OPC_TOTEM_gc;
#endif // 1


  // --------------------
  // INTERRUPT CONTROLLER
  // --------------------

  // FINALLY, set up the interrupt controller for priority-based interrupts
  // and enable them.  Important.  See 10.8.3 in D manual

  CCP = CCP_IOREG_gc; // 0xd8 - see D manual, sect 3.14.1 (protected I/O)
  PMIC_CTRL = 0x87; // binary 10000111  all 3 int levels enabled, round-robin enabled, IVT starts at 0002H



  adc_setup(); // set up the ADC (function exported from wiring_analog.c)

  // this needs to be called before setup() or some functions won't
  // work there
  sei();
}
