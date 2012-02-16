#include  "msp430g2553.h"

#define P_TIM_LED 	(0x40) // Green LED
#define P_ADC_LED 	(0x01)
#define P_TX		(BIT2)

#define NUMBER_OF_ACQUISITIONS	(200)

// A tick represent an interruption by timerA. TIME_BETWEEN_TICK represent the time interval between 2 interruptions
// WARNING ! TIME_BETWEEN_TICK must be <= 16 (why ? because 17 * 32768 / 8 don't fit in the 16 bits register TACCR0
#define TIME_BETWEEN_TICK (15)

// Start the ADC every X tick. THe "time" variable is incremented at every ADC
#define TICK_MULT (4)

// Number of : TIME_BETWEEN_TICK * TICK_MULT
volatile unsigned char currentTime; // max : ~4 hours for an 1 minute interval (TIME_BETWEEN_TICK = 15, TICK_MULT = 4)
volatile unsigned char currentTick; // Goes from 0 to TICK_MULT, then back to 0

// Store the data as follow : [1 char for the time elapsed since the last data, 1 char for the voltage]
volatile unsigned char measurements[NUMBER_OF_ACQUISITIONS*2];

volatile unsigned int currentMeasurement;

void inline init() {
	// Disable Watchdog Timer
	WDTCTL = WDTPW + WDTHOLD;
}

void setupTimer() {
	// Enable interrupt after "TACCR0" cycles
	TACCR0 = (32768 * TIME_BETWEEN_TICK) / 8; // (trigger every TIME_BETWEEN_TICK seconds, using a /8 divider). 16 bits only
	// Compare-mode interrupt.
	TACCTL0 |= CCIE;

	// TASSEL_1		: Use ACLK, the external clock
	// MC_1			: Timer goes from 0 to TACCTL0
	TACTL = TASSEL_1 + MC_1 + ID_3;                  // TACLK = ACLK, Up mode, /8 divider.

	/*
	 * You can select sleep mode LPM3 if you have an external clock. CPU & All clock are disabled except ACLK.
	 * Use LPM0 otherwise. See User Guide p. 42

	 	 START TIMER USING :

	 	 TIMER interrupt service routine (define outside main) :
	 	 	 #pragma vector=TIMER0_A0_VECTOR
				__interrupt void ta0_isr(void)
				{
					LPM3_EXIT;          // Exit LPM3 on return
				}
	 *
	 */
}

void inline setupADCSingle() {
	// Select Channel
	ADC10CTL1 = INCH_4;

	// ADC10ON		: Enable the ADC module
	// ADC10SR		: ADC sampling rate (50ksps if activated, 200 otherwise)
	// SREF_1 		: Set references, V+ = VREF+, V- = VSS
	// ADC10SHT_2 	: Sample-and-hold time : 16 × ADC10CLKs (ADC10SHT_3 : 64 × ADC10CLKs)
	// REFON 		: Reference generator on
	// REF2_5V 		: Reference-generator voltage (2.5V if activated, 1.5 otherwise).
	// ADC10IE		: Generate an interrupt when sampling is over.
	ADC10CTL0 = ADC10ON | ADC10SR | SREF_1 | ADC10SHT_3 | REFON | REF2_5V | ADC10IE;

	/*
	  	START ADC USING : (ENC: enable conversion, ADC10SC : start conversion)
			// Don't forget to enable interrupts !
			_EINT()
			ADC10CTL0 |= ENC + ADC10SC;
			// Go to sleep mode until interrupt is triggered
			LPM0
			// Result is stored in ADC10MEM

		ADC10 interrupt service routine (define outside main) :
			#pragma vector=ADC10_VECTOR
			__interrupt void ADC10_ISR(void)
			{
				LPM0_EXIT;
			}
	 */
}

/*
 * All times are relative to the previous one.
 * One "time" represents TIME_BETWEEN_TICK * TICK_MULT seconds
 *
 * Protocol :
 * Number of measurements (char)
 * time for measurement 1 (char, should be 1)
 * Voltage for measurement 1 (char)
 * time between measurement 1 and 2 (char)
 * Voltage for measurement 2 (char)
 * time between measurement 2 and 3 (char)
 * Voltage for measurement 3 (char)
 * ...
 * ...
 * 0xff (char) as end-of-data
 */
void inline sendUARTData() {

	int i;

	while (!(IFG2&UCA0TXIFG));             // USCI_A0 TX buffer ready?
	UCA0TXBUF = currentMeasurement;        // TX -> RXed character

	for(i=0; i< currentMeasurement*2; i++) {
		while (!(IFG2&UCA0TXIFG));         // USCI_A0 TX buffer ready?
		  UCA0TXBUF = measurements[i];	   // TX -> RXed character
	}

	while (!(IFG2&UCA0TXIFG));             // USCI_A0 TX buffer ready?
	UCA0TXBUF = 0xff;        // Send end of Data
}

/**
 * 9600 bauds, 8 bits, no flow control
 */
void inline setupUART() {
	  UCA0CTL1 = UCSSEL_1 | UCSWRST;                     // CLK = ACLK
	  UCA0CTL0 = 0;					// UART ASYNC MODE, 8N1, LSB
	  UCA0BR0 = 0x03;                           // (LSB) 32kHz/9600 = 3
	  UCA0BR1 = 0x00;                           // (MSB)
	  UCA0MCTL			=	UCBRS1 + UCBRS0;

	  P1SEL = P_TX;								// P1SEL to 1 and P1SEL2 to 1 => "Secondary peripheral module function is selected"
	  P1SEL2 = P_TX;

	  UCA0CTL1 &= ~UCSWRST;                     // **Initialize USCI state machine**** /
}

void main()
{
	int i;
	// Filling it with known data to detect bugs
	for(i=0; i<NUMBER_OF_ACQUISITIONS;i++) measurements[i] = 0xaa;

	currentTime = 0;
	currentTick = 0;

	init();
	setupADCSingle();
	setupTimer();
	setupUART();

	// Selection of the "output mode" for the LED
	P1DIR = P_TIM_LED | P_ADC_LED | P_TX; // TX

	//Start LED ADC
	P1OUT |= P_ADC_LED; //

	// Working on first line of data
	currentMeasurement = 0;

	// Enable Interrupts
	_EINT();
	while (currentMeasurement < NUMBER_OF_ACQUISITIONS) {
		LPM0; 						// Wait for the TIMER triggered wake-up
		if(currentTick == 0) { // currentTime incremented : start a new ADC
			ADC10CTL0 |= ENC + ADC10SC;	// Sampling and conversion start
			LPM0; // Wait for the end of the conversion
		}
		// Send UART data anyway : all the data is transmitted every TIME_BETWEEN_TICK
		sendUARTData();
	  }
}



// ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR(void)
{
	char newValue, oldValue;
	unsigned int currentPos = 2*currentMeasurement;  // current position in the array

	newValue = (char)( 0xff & (ADC10MEM >> 2) ); // Put the result on 8 bits

	if(currentPos > 0)
		oldValue = measurements[currentPos-1]; // Take the PREVIOUS one
	else
		oldValue = 0xff; // Initialization for the first measurement

	if( newValue < oldValue) {
		measurements[currentPos] = currentTime; 		// add time
		measurements[currentPos+1] = newValue;	// Add ADC data
		currentMeasurement ++;
		currentTime = 0; // Reset time
	}

	P1OUT ^= P_ADC_LED;			// Change state of ADC LED
	LPM0_EXIT; // Need to exit low poser mode to send data
}


// This function is called every TIME_BETWEEN_TICK seconds
#pragma vector=TIMER0_A0_VECTOR
__interrupt void ta0_isr(void)
{
	if(currentTick < TICK_MULT - 1) {
		currentTick ++;
	} else {
		currentTick = 0;
		currentTime ++;
	}
	P1OUT ^= P_TIM_LED;	// Blink LED (turn on if off, turn off if on)
	LPM0_EXIT;          // Exit LPM0 on return
}
