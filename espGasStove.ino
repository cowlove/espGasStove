#include "jimlib.h"

JStuff j;

struct {
	int led = getLedPin(); 
	int pwm = 17;
	int temp = 16;
} pins;

TempSensor temp(pins.temp);
PwmChannel pwm(pins.pwm, 50, 1, 2);

CLI_VARIABLE_FLOAT(setTemp, 19.5);
CLI_VARIABLE_FLOAT(hist, 0.15);
CLI_VARIABLE_INT(useLED, 0);


string testHook("hi");
float lastTemp = 0.0;

void setup() {
	j.begin();
	j.cli.hookVar("HOOK", &testHook);
	j.cli.on("PWM ([0-9]+)", [](const char *, smatch m){ 
		if (m.size() > 1) 
			pwm.setMs(atoi(m.str(1).c_str()));
		return strfmt("%f", pwm.get()); 
	});
}

void loop() {
	j.run();

	if (j.secTick(60)) {
		float t = temp.readTemp();
		if (t > setTemp && (lastTemp == 0 || lastTemp <= setTemp)) { 
			OUT("stove off");
			pwm.setMs(900);
		} else 	if (t < setTemp - hist && (lastTemp == 0 || lastTemp >= setTemp - hist)) {
			OUT("stove on"); 
			pwm.setMs(1700);
			delay(3000);
			pwm.setMs(1600);
		} 
		lastTemp = t;
		OUT("temp %6.3f pwm %.3f", t, pwm.get());
	}
}
