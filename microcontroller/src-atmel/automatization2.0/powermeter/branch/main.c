#include <avr/io.h>
#include <stdio.h>
#include <string.h>
#include <avr/interrupt.h>

#include "can_handler.h"
#include "can/spi.h"
#include "can/can.h"
#include "adc_driver.h"
#include "led_driver.h"
#include "config.h"
#include "powermeter_driver.h"
#include "ursartC1_driver.h"
#include "event_system_driver.h"
#include "rtc_driver.h"
#include "wdt_driver.h"

uint8_t error = 0;

void sync_osc()
{
	/*32 MHz Oszillator starten */
	OSC.CTRL |= OSC_RC32MEN_bm;

	/*Wenn Oscillator stabil wird das Flag RC32MRDY
	 * gesetzt und 32Mhz können benutzt werden*/
	while (!(OSC.STATUS & OSC_RC32MRDY_bm));

	/* auto kalibrierung ein */
	DFLLRC32M.CTRL = DFLL_ENABLE_bm;

	/*I/O Protection*/
	CCP = CCP_IOREG_gc;
	/*Clock auf 32Mhz einstellen*/
	CLK.CTRL = CLK_SCLKSEL_RC32M_gc;
}

void start_mcp_clock()
{
	//init the timer for mcp2515 clock
	PORTD.DIRSET = (1<<2);
	TCD0.CTRLB = TC0_CCCEN_bm | 3; //single slope pwm, OC0C as output
	TCD0.PER = 1;
	TCD0.CCC = 1;
	TCD0.CNT = 0;
#if F_MCP == 16000000
	TCD0.CTRLA = 1; //clk/2
#elif F_MCP == 8000000
	TCD0.CTRLA = 2; //clk/2
#endif
}

void Eventsystem_init(void)
{
	/* Select multiplexer input. */
	EVSYS_SetEventSource( 0, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 1, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 2, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 3, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 4, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 5, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 6, EVSYS_CHMUX_OFF_gc);
	EVSYS_SetEventSource( 7, EVSYS_CHMUX_TCC1_OVF_gc);	//event Timer1CC1_OVF
}

void Interrupt_Init(void)
{
	//enable ROUND ROBIN,enable MED_LVL & LOW_LVL interrupts !!!!
	uint8_t tmp = PMIC_RREN_bm | PMIC_MEDLVLEN_bm | PMIC_LOLVLEN_bm;
	/*I/O Protection*/
	CCP = CCP_IOREG_gc;
	PMIC.CTRL = tmp;

	sei();	//global allow interrupts
}


/* User defined RTC Init */
void RTC_Init( void )
{
	/* Security Signature to modify clock */
	//CCP = CCP_IOREG_gc;

	/* Turn on internal 32kHz. */
	//OSC.CTRL |= OSC_RC32KEN_bm;

	do {
		/* Wait for the 32kHz oscillator to stabilize. */
	} while ((OSC.STATUS & OSC_RC32KRDY_bm) == 0);


	/* Set internal 32kHz oscillator as clock source for RTC. */
	CLK.RTCCTRL = CLK_RTCSRC_RCOSC_gc | CLK_RTCEN_bm;

	do {
		/* Wait until RTC is not busy. */
	} while (RTC_Busy());

	/* Configure RTC period to 1 second. */
	RTC_Initialize(RTC_CYCLES_1S, 0, 0, RTC_PRESCALER_DIV1_gc);

	/* Enable overflow interrupt. */
	RTC_SetIntLevels(RTC_OVFINTLVL_LO_gc, RTC_COMPINTLVL_OFF_gc);

}

ISR(RTC_OVF_vect)
{
	powermeter.seconds_uptime++;
}

int main(void)
{
	sync_osc(); //start HSE osc

        LED_initPORTC();  // LED Ports als Ausgang
        LED_off();
        LED__GREEN(1);

	start_mcp_clock();
	spi_init();
	can_init();
	read_can_addr();

	//init watchdog
	WDT_EnableAndSetTimeout(WDT_PER_64CLK_gc);

	RTC_Init();					//enable 1sec interrupt

        Eventsystem_init();
        Interrupt_Init();
	powermeter_Start();

	uint16_t x = 0;
	while (1) {
		can_handler();
		WDT_Reset();

		powermeter_docalculations();
		WDT_Reset();

		can_handler();
		WDT_Reset();

		if( checkforcanupdate() )
		{
			can_createDATAPACKET();
		}
		WDT_Reset();

		if ((RTC.CNT & 0x00ff) >= x)
			x = RTC.CNT;
		else
		{		//this is executed 4 times per second
			if(error)
			{
				LED__RED(1);
			}
			else
			{
				LED__RED(0);
			}
		}
	}
}
