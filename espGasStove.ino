#include "jimlib.h"

JStuff j;
#define OUT j.out

struct {
	int led = getLedPin(); 
	int pwm = 17;
	int temp = 16;
} pins;

TempSensor temp(pins.temp);
PwmChannel pwm(pins.pwm, 50, 0, 2);
Timer sec(2000), minute(60000), blink(100);
float setTemp = 19.5, hist =0.15;
float lastTemp = 0.0;
int useLED = 0;

string testHook("hi");

void setup() {
	j.begin();
	j.cli.hookVar("hook", &testHook);
	j.cli.hookVar("temp", &setTemp);
	j.cli.hookVar("hist", &hist);
	j.cli.hookVar("led", &useLED);
	j.cli.on("pwm ([0-9]+)", [](smatch m){ pwm.setMs(atoi(m.str(1).c_str()));});
	j.cli.on("MINUTE", [](){ minute.alarmNow(); });
	minute.alarmNow();	
}

void loop() {
	j.run();

	if (blink.tick()) { 
		//OUT("%6.3f SET: %6.3f, HIST: %6.3f LED: %d", temp.readTemp(), setTemp, hist, useLED);
		if (useLED) 
			digitalToggle(pins.led);
		else
			digitalWrite(pins.led, 0);
	}

	if (minute.tick()) {
		float t = temp.readTemp();
		if (t > setTemp && (lastTemp == 0 || lastTemp <= setTemp)) { 
			pwm.setMs(900);
		} else 	if (t < setTemp - hist && (lastTemp == 0 || lastTemp >= setTemp - hist)) { 
			pwm.setMs(1700);
			delay(3000);
			pwm.setMs(1600);
		} 
		lastTemp = t;
		OUT("temp %6.3f pwm %.3f", t, pwm.get());
	}
}
