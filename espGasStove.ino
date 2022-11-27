#include "jimlib.h"

JStuff j;

struct {
	int led = getLedPin(); 
	int pwm = 17;
	int temp = 16;
	int adc = 34;
} pins;

TempSensor temp(pins.temp);
PwmChannel pwm(pins.pwm, 50/*hz*/, 1/*chan*/, 0/*gradual*/);

CLI_VARIABLE_FLOAT(setTemp, 5);
CLI_VARIABLE_FLOAT(hist, 0.05);
CLI_VARIABLE_INT(useLED, 0);


string testHook("hi");
float lastTemp = 0.0, lastSetTemp = 0.0;

void setup() {
	j.begin();
	j.cli.hookVar("HOOK", &testHook);
	j.cli.on("PWM ([0-9]+)", [](const char *, smatch m){ 
		if (m.size() > 1) 
			pwm.setMs(atoi(m.str(1).c_str()));
		return strfmt("%f", pwm.get()); 
	});
	j.cli.on("GRADUAL ([0-9]+)", [](const char *, smatch m) { 
		if (m.size() > 1) 
			pwm.gradual = atoi(m.str(1).c_str());
		return strfmt("%d", pwm.gradual); 
	});
	
}

void loop() {
	j.run();
	if (j.secTick(1)) { 
		LOG(2,"pwm %f adc %d", pwm.get(), analogRead(pins.adc));
	}
	if (setTemp != lastSetTemp) { 
		lastTemp = 0;
		lastSetTemp = setTemp;
		j.forceTicks();
	}
	if (j.secTick(60)) {
		float t = temp.readTemp();
		// TODO doesn't handle changing setTemp 
		if (t > setTemp && (lastTemp == 0 || lastTemp <= setTemp)) { 
			OUT("stove off");
			pwm.setMs(900);
		} else 	if (t < setTemp - hist && (lastTemp == 0 || lastTemp >= setTemp - hist)) {
			OUT("stove on"); 
			pwm.setMs(2000);
			delay(1000);
			pwm.setMs(1600);
		} 
		lastTemp = t;
		OUT("set %.3f temp %.3f pwm %.3f adc %d uptime %.2f" , (double)setTemp, t, pwm.get(), analogRead(pins.adc), millis()/1000.0/60.0);
	}
	delay(10);  
}
