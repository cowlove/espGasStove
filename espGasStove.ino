#include "jimlib.h"
#include <ArduinoJson.h>
#include "OneWireNg_CurrentPlatform.h"
#include <utility>
#include <regex>
#include <esp_task_wdt.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>


using namespace std;


class CommandLineInterface { 
	typedef std::function<string(const char *,std::smatch)> callback;
	std::vector<std::pair<std::string, callback>> handlers;
public:

	CommandLineInterface() { 
	}

	void on(const char *pat, callback h) { 
		handlers.push_back(std::pair<std::string, callback>(pat, h));
	}
	void on(const char *pat, std::function<void()> f) { 
		on(pat, [f](const char *, std::smatch) { f(); return string(); });
	}
	void on(const char *pat, std::function<void(std::smatch)> f) { 
		on(pat, [f](const char *, std::smatch m) { f(m); return string(); });
	}
	void on(const char *pat, std::function<void(const char *l)> f) { 
		on(pat, [f](const char *l, std::smatch) { f(l); return string(); });
	}
	string process(const char *line) {
		string rval = ""; 
		if (strstr(line, "hooks")) { 
			smatch dummy;
			for(auto i = handlers.begin() + 1; i != handlers.end(); i++) { 
				rval += i->first + " \t " + i->second("", dummy) + "\n";
			}
			return rval;
		}
		for(auto i = handlers.begin(); i != handlers.end(); i++) { 
			std::smatch res;
			std::regex exp((i->first).c_str());
			std::string str = line;
			if (std::regex_match(str, res, exp))  
				rval += i->second(line, res);
			}
		return rval;
	}
	template<typename T>
	void hookRaw(const char *pat, T* v) {
		const char *fmt = formatOf<T>();
		on(pat, [v,fmt](const char *, smatch m) { 
			if (m.size() > 1) 
				sscanf(m.str(1).c_str(), fmt, v);
			return strfmt(fmt, *v);
		});
	}

	template<typename T>
	void hookVar(const char *l, T*p) {
		std::string s = strfmt("set %s=(.*)", l);
		hookRaw(s.c_str(), p);
		s = strfmt("get %s", l);
		hookRaw(s.c_str(), p);	
	}

	template<typename T> const char *formatOf();
};

template<> const char *CommandLineInterface::formatOf<float>() { return "%f"; }
template<> const char *CommandLineInterface::formatOf<int>() { return "%i"; }

template<>
void CommandLineInterface::hookRaw<string>(const char *pat, string *v) {
	on(pat, [v](const char *, smatch m) { 
		if (m.size() > 1)
			*v = m.str(1).c_str();
		return (*v).c_str();
	});
}

class JStuff {		
	bool parseSerial;
	std::function<void()> onConn = NULL;
	LineBuffer lb;
	bool debug = false;
public:
	CommandLineInterface cli;
	JStuff(bool ps = true) : parseSerial(ps) {
		cli.on("DEBUG", [this]() { debug = true;});
	}
	JimWiFi jw;
	MQTTClient mqtt = MQTTClient("192.168.4.1", basename(__BASE_FILE__).c_str());
	void run() { 
		esp_task_wdt_reset();
		jw.run(); 
		mqtt.run(); 
		while(parseSerial == true && Serial.available()) { 
			lb.add(Serial.read(), [this](const char *l) {
				string r = cli.process(l);
				Serial.println(r.c_str());
			});
		}
	}
	void begin() { 
		esp_task_wdt_init(60, true);
		esp_task_wdt_add(NULL);

		Serial.begin(921600, SERIAL_8N1);
		Serial.println(__BASE_FILE__ " " GIT_VERSION);
		getLedPin();

		jw.onConnect([this](){
			jw.debug = mqtt.active = (WiFi.SSID() == "ChloeNet");
			Serial.printf("WiFi connected to %s\n", WiFi.SSID().c_str());
			if (onConn != NULL) { 
				onConn();
			}
		});
		mqtt.setCallback([this](String t, String m) {
			string r = cli.process(m.c_str());
			mqtt.pub(r.c_str());
		});
	}
	void out(const char *format, ...) { 
		va_list args;
		va_start(args, format);
		char buf[256];
		vsnprintf(buf, sizeof(buf), format, args);
		va_end(args);
		mqtt.pub(buf);
		Serial.println(buf);
		jw.udpDebug(buf);
	}
};



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
	int pwm = -1;
	int gradual;
public:		
	PwmChannel(int p, int hz = 50, int c = 0, int g = 0) : pin(p), channel(c), gradual(g) {
		ledcSetup(channel, hz, 16);
		ledcAttachPin(pin, channel);
	}
	void setMs(int p) { set(p * 4715 / 1500); };
	void setPercent(int p) { set(p * 65535 / 100); } 
	void set(int p) { 
		while(gradual && pwm != -1 && pwm != p) { 
			ledcWrite(channel, pwm);  
			pwm += pwm < p ? 1 : -1;
			delay(gradual);
		}
		ledcWrite(channel, p); 
		pwm = p; 
	} 
	float get() { return (float)pwm / 65535; } 
};

void digitalToggle(int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, !digitalRead(pin)); }



JStuff j;
#define OUT j.out


struct {
	int led = getLedPin(); // D1 mini
	int pwm = 17;
	int temp = 16;
} pins;

TempSensor temp(pins.temp);
PwmChannel pwm(pins.pwm, 50, 0, 2);
Timer sec(2000), minute(60000), blink(100);
float setTemp = 22, hist =0.15;
float lastTemp = 0.0;
int useLED = 1;
template<typename T> T parseMatch(std::string s);

float parseFloat(const char *p) { 
	float rval = 1;
	sscanf("%f", p, &rval);
	return rval;
}

string testHook("hi");

void setup() {
	j.begin();
	j.cli.hookVar("hook", &testHook);
	j.cli.hookVar("temp", &setTemp);
	j.cli.hookVar("hist", &hist);
	j.cli.hookVar("led", &useLED);
	j.cli.on("MINUTE", [](){ minute.alarmNow(); });
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
