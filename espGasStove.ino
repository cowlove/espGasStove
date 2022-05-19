#include "jimlib.h"
#include <ArduinoJson.h>
#include "OneWireNg_CurrentPlatform.h"

#include <esp_task_wdt.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>

struct {
	int led = getLedPin(); // D1 mini
	int pwm = 17;
	int temp = 16;
} pins;

class JStuff {
public:
	JimWiFi jw;
	MQTTClient mqtt = MQTTClient("192.168.4.1", basename(__BASE_FILE__).c_str());
	void run() { 
		esp_task_wdt_reset();
		jw.run(); 
		mqtt.run(); 
	}
	void begin() { 
		esp_task_wdt_init(60, true);
		esp_task_wdt_add(NULL);

		Serial.begin(921600, SERIAL_8N1);
		Serial.println(__BASE_FILE__ " " GIT_VERSION);
		getLedPin();

		jw.onConnect([this](){
			jw.debug = mqtt.active = (WiFi.SSID() == "ChloeNet");
		});
		mqtt.setCallback([](String t, String m) {
				dbg("MQTT got string '%s'", m.c_str());
		});
	}
};

JStuff j;


class TempSensor { 
	public:
	#define OWNG OneWireNg_CurrentPlatform 
	OWNG *ow = NULL; 
	TempSensor(OWNG *p) : ow(p) {}
	TempSensor(int pin, bool pullup = true) {
		ow = new OWNG(pin, pullup);
	}
	float readTemp() { 
			std::vector<DsTempData> t = readTemps(ow);
			return t.size() > 0 ? t[0].degC : 0;
	}
};

class PwmChannel {
	int pin; 
	int channel;
	int pwm;
public:		
	PwmChannel(int p, int hz = 50, int c = 0) : pin(p), channel(c) {
		ledcSetup(channel, hz, 16);
		ledcAttachPin(pin, channel);
	}
	void setMs(int p) { set(p * 4715 / 1500); };
	void setPercent(int p) { set(p * 65535 / 100); } 
	void set(int p) { ledcWrite(channel, p); pwm = p; } 
	float get() { return (float)pwm / 65535; } 
};

void digitalToggle(int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, !digitalRead(pin)); }

TempSensor temp(pins.temp);
PwmChannel pwm(pins.pwm);

void setup() {
	j.begin();
}

EggTimer sec(2000), minute(60000);
EggTimer blink(100);
LineBuffer lb;
float setTemp = 23.0;
float lastTemp;

void loop() {
	j.run();

	while(Serial.available()) { 
		lb.add(Serial.read(), [](const char *l) {
			int p;
			if (sscanf(l, "%d", &p) == 1) { 
				pwm.setMs(p);	
			}
		});
	}
	if (blink.tick()) { 
		float t = temp.readTemp();
		Serial.printf("%6.3f\n", t);
		digitalToggle(pins.led);
	}
	if (minute.tick()) {
		float t = temp.readTemp();
		if (t > setTemp && lastTemp <= setTemp) { 
			pwm.setMs(900);
			delay(1000);
			pwm.setMs(1000);
		} else 	if (t < setTemp - .5 && lastTemp >= setTemp - .5) { 
			pwm.setMs(1900);
			delay(3000);
			pwm.setMs(1500);
		} 
		lastTemp = t;
		j.mqtt.pub(Sfmt("temp %6.3f pwm %.3f", t, pwm.get()).c_str());
	}
}
