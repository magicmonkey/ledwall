// arduino-cli compile -b adafruit:samd:adafruit_feather_m0 -u -p /dev/ttyACM0

#include <MM_DotStar.h>
#include <SPI.h>
#include <WiFi101.h>
#include "secrets.h"
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>

#include "wiring_private.h" // pinPeripheral function

#define NUMPIXELS 238
#define EDGE_SIZE 14 * 7
#define MODE_SNAKE 0
#define MODE_BURST 1

char wifi_ssid[] = SECRET_SSID;
char wifi_pass[] = SECRET_PASS;

uint8_t start_brightness = 100;
char debug[100];
int lastLaunch = 0;
float decayFactor = 0.9;
long loopTime, lastWifiPing, timeToSleep;

int pingResult;

SPIClass SPI2(&sercom1, 12, 13, 11, SPI_PAD_0_SCK_1, SERCOM_RX_PAD_3);

MM_DotStar strip(NUMPIXELS, &SPI2, DOTSTAR_BGR);
uint32_t bg = strip.Color(0, 0, 0);

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
StaticJsonDocument<100> jsonBuffer;

int fireworkTick = 3000;
int snakeNum = -1;

/*
    7       0
   . \     / .
  q   p   g   h
 /     . .     \
8       5       1
|.     .|\     .|
m n   j f i   c b
.  \ /  .  . /  .
9   11  4   6   2
 \  .  / \  .  /
  l k o   e d a
   .|.     .|.
    10      3
*/

typedef struct {
	uint16_t hue;
	uint8_t sat;
	uint8_t val;
} pixel;
pixel pixelStrip[NUMPIXELS];

typedef struct edge {
	pixel *pixels; // Pointer to the first of 14 pixels in the pixelStrip array
	struct edge *next[5];
	int direction; // 1 or -1 for if the edge goes forwards or backwards along the pixelStrip array
} edge;

typedef struct {
	uint16_t hue;
	uint8_t sat;
	uint8_t brightness;
	edge *e;
	int positionOnEdge;
	int state; // 0 = Stopped, 1 = Running
	int speed; // 0-7
	int mode;  // 0 = Snake, 1 = Burst
	float decayFactor;
} head;

edge a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q;
edge A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q;

void initLattice() {

	a.pixels = &pixelStrip[14 * 0];  a.direction = 1;
	b.pixels = &pixelStrip[14 * 1];  b.direction = 1;
	c.pixels = &pixelStrip[14 * 2];  c.direction = 1;
	d.pixels = &pixelStrip[14 * 3];  d.direction = 1;
	e.pixels = &pixelStrip[14 * 4];  e.direction = 1;
	f.pixels = &pixelStrip[14 * 5];  f.direction = 1;
	g.pixels = &pixelStrip[14 * 6];  g.direction = 1;
	h.pixels = &pixelStrip[14 * 7];  h.direction = 1;
	i.pixels = &pixelStrip[14 * 8];  i.direction = 1;
	j.pixels = &pixelStrip[14 * 9];  j.direction = 1;
	k.pixels = &pixelStrip[14 * 10]; k.direction = 1;
	l.pixels = &pixelStrip[14 * 11]; l.direction = 1;
	m.pixels = &pixelStrip[14 * 12]; m.direction = 1;
	n.pixels = &pixelStrip[14 * 13]; n.direction = 1;
	o.pixels = &pixelStrip[14 * 14]; o.direction = 1;
	p.pixels = &pixelStrip[14 * 15]; p.direction = 1;
	q.pixels = &pixelStrip[14 * 16]; q.direction = 1;

	A.pixels = &pixelStrip[(14 * 1)  - 1]; A.direction = -1;
	B.pixels = &pixelStrip[(14 * 2)  - 1]; B.direction = -1;
	C.pixels = &pixelStrip[(14 * 3)  - 1]; C.direction = -1;
	D.pixels = &pixelStrip[(14 * 4)  - 1]; D.direction = -1;
	E.pixels = &pixelStrip[(14 * 5)  - 1]; E.direction = -1;
	F.pixels = &pixelStrip[(14 * 6)  - 1]; F.direction = -1;
	G.pixels = &pixelStrip[(14 * 7)  - 1]; G.direction = -1;
	H.pixels = &pixelStrip[(14 * 8)  - 1]; H.direction = -1;
	I.pixels = &pixelStrip[(14 * 9)  - 1]; I.direction = -1;
	J.pixels = &pixelStrip[(14 * 10) - 1]; J.direction = -1;
	K.pixels = &pixelStrip[(14 * 11) - 1]; K.direction = -1;
	L.pixels = &pixelStrip[(14 * 12) - 1]; L.direction = -1;
	M.pixels = &pixelStrip[(14 * 13) - 1]; M.direction = -1;
	N.pixels = &pixelStrip[(14 * 14) - 1]; N.direction = -1;
	O.pixels = &pixelStrip[(14 * 15) - 1]; O.direction = -1;
	P.pixels = &pixelStrip[(14 * 16) - 1]; P.direction = -1;
	Q.pixels = &pixelStrip[(14 * 17) - 1]; Q.direction = -1;

	a.next[0] = &b; a.next[1] = NULL; a.next[2] = NULL; a.next[3] = NULL; a.next[4] = NULL;
	b.next[0] = &c; b.next[1] = &H;   b.next[2] = NULL; b.next[3] = NULL; b.next[4] = NULL;
	c.next[0] = &d; c.next[1] = &i;   c.next[2] = NULL; c.next[3] = NULL; c.next[4] = NULL;
	d.next[0] = &e; d.next[1] = &a;   d.next[2] = NULL; d.next[3] = NULL; d.next[4] = NULL;
	e.next[0] = &f; e.next[1] = &O;   e.next[2] = NULL; e.next[3] = NULL; e.next[4] = NULL;
	f.next[0] = &j; f.next[1] = &I;   f.next[2] = &g;   f.next[3] = &p;   f.next[4] = NULL;
	g.next[0] = &h; g.next[1] = NULL; g.next[2] = NULL; g.next[3] = NULL; g.next[4] = NULL;
	h.next[0] = &B; h.next[1] = &c;   h.next[2] = NULL; h.next[3] = NULL; h.next[4] = NULL;
	i.next[0] = &F; i.next[1] = &g;   i.next[2] = &j;   i.next[3] = &p;   i.next[4] = NULL;
	j.next[0] = &k; j.next[1] = &N;   j.next[2] = NULL; j.next[3] = NULL; j.next[4] = NULL;
	k.next[0] = &l; k.next[1] = &o;   k.next[2] = NULL; k.next[3] = NULL; k.next[4] = NULL;
	l.next[0] = &m; l.next[1] = NULL; l.next[2] = NULL; l.next[3] = NULL; l.next[4] = NULL;
	m.next[0] = &n; m.next[1] = &Q;   m.next[2] = NULL; m.next[3] = NULL; m.next[4] = NULL;
	n.next[0] = &J; n.next[1] = &k;   n.next[2] = NULL; n.next[3] = NULL; n.next[4] = NULL;
	o.next[0] = &E; o.next[1] = &f;   o.next[2] = NULL; o.next[3] = NULL; o.next[4] = NULL;
	p.next[0] = &q; p.next[1] = NULL; p.next[2] = NULL; p.next[3] = NULL; p.next[4] = NULL;
	q.next[0] = &M; q.next[1] = &n;   q.next[2] = NULL; q.next[3] = NULL; q.next[4] = NULL;

	A.next[0] = &D; A.next[1] = &e;   A.next[2] = NULL; A.next[3] = NULL; A.next[4] = NULL;
	B.next[0] = &A; B.next[1] = NULL; B.next[2] = NULL; B.next[3] = NULL; B.next[4] = NULL;
	C.next[0] = &B; C.next[1] = &H;   C.next[2] = NULL; C.next[3] = NULL; C.next[4] = NULL;
	D.next[0] = &i; D.next[1] = &C;   D.next[2] = NULL; D.next[3] = NULL; D.next[4] = NULL;
	E.next[0] = &a; E.next[1] = &D;   E.next[2] = NULL; E.next[3] = NULL; E.next[4] = NULL;
	F.next[0] = &E; F.next[1] = &O;   F.next[2] = NULL; F.next[3] = NULL; F.next[4] = NULL;
	G.next[0] = &I; G.next[1] = &F;   G.next[2] = &j;   G.next[3] = &p;   G.next[4] = NULL;
	H.next[0] = &G; H.next[1] = NULL; H.next[2] = NULL; H.next[3] = NULL; H.next[4] = NULL;
	I.next[0] = &C; I.next[1] = &d;   I.next[2] = NULL; I.next[3] = NULL; I.next[4] = NULL;
	J.next[0] = &p; J.next[1] = &g;   J.next[2] = &I;   J.next[3] = &F;   J.next[4] = NULL;
	K.next[0] = &J; K.next[1] = &N;   K.next[2] = NULL; K.next[3] = NULL; K.next[4] = NULL;
	L.next[0] = &o; L.next[1] = &K;   L.next[2] = NULL; L.next[3] = NULL; L.next[4] = NULL;
	M.next[0] = &L; M.next[1] = NULL; M.next[2] = NULL; M.next[3] = NULL; M.next[4] = NULL;
	N.next[0] = &M; N.next[1] = &Q;   N.next[2] = NULL; N.next[3] = NULL; N.next[4] = NULL;
	O.next[0] = &l; O.next[1] = &K;   O.next[2] = NULL; O.next[3] = NULL; O.next[4] = NULL;
	P.next[0] = &g; P.next[1] = &j;   P.next[2] = &F;   P.next[3] = &I;   P.next[4] = NULL;
	Q.next[0] = &P; Q.next[1] = NULL; Q.next[2] = NULL; Q.next[3] = NULL; Q.next[4] = NULL;
}

#define NUMHEADS 100
head heads[NUMHEADS];

int startSnake(edge *newEdge, uint16_t hue, int speed, int mode, uint8_t brightness) {
	int spareSnake = 0;
	for (int i=0; i<NUMHEADS; i++) {
		if (heads[i].state == 0) {
			spareSnake = i;
			break;
		}
	}

	heads[spareSnake].brightness = brightness;
	heads[spareSnake].sat = 255;
	heads[spareSnake].hue = hue;
	heads[spareSnake].positionOnEdge = 0;
	heads[spareSnake].e = newEdge;
	heads[spareSnake].state = 1;
	heads[spareSnake].speed = speed;
	heads[spareSnake].mode = mode;
	heads[spareSnake].decayFactor = 0.95; // Only used for burst mode, not snake

	return spareSnake;
}

void advanceSnake(head *thisHead) {

	thisHead->positionOnEdge += thisHead->speed;

	if (thisHead->positionOnEdge >= EDGE_SIZE) {

		thisHead->positionOnEdge = 0;
		int next = random(0, 5);
		if (thisHead->e->next[next] != NULL) {
			thisHead->e = thisHead->e->next[next];
		} else {
			thisHead->e = thisHead->e->next[0];
		}

		thisHead->hue = thisHead->hue + (random(0, 4000) - 2000);

	}
}

void advanceBurst(head *thisHead) {
	if (random(0, 10) == 0) {
		thisHead->brightness *= thisHead->decayFactor;
	}
	if (thisHead->brightness < 2) {
		thisHead->state = 0;
	}
	if (random(0, 30) == 0) {
		thisHead->speed--;
		if (thisHead->speed <= 0) {
			thisHead->state = 0;
		}
	}

	thisHead->positionOnEdge += thisHead->speed;

	if (thisHead->positionOnEdge >= EDGE_SIZE) {
		if (thisHead->speed > 0) {
			for (int j=0; j<=4; j++) {
				if (thisHead->e->next[j] != NULL) {
					startSnake(thisHead->e->next[j], thisHead->hue, thisHead->speed, thisHead->mode, thisHead->brightness);
				}
			}
		}
		thisHead->state = 0;
	}

}

void advance() {
	for (int i=0; i<NUMHEADS; i++) {
		if (heads[i].state == 0) {
			continue;
		}
		switch (heads[i].mode) {
			case MODE_SNAKE:
				advanceSnake(&heads[i]);
				break;
			case MODE_BURST:
				advanceBurst(&heads[i]);
				break;
		}
	}

	// Do we need to launch a new burst?
	if (fireworkTick > 0 && ((millis() - lastLaunch) > fireworkTick)) {
		launchFirework();
	}
}

void enableSnake() {
	if (snakeNum == -1) {
		snakeNum = startSnake(&a, 10000, 2, MODE_SNAKE, start_brightness);
	}
}

void disableSnake() {
	if (snakeNum >= 0) {
		heads[snakeNum].state = 0;
		snakeNum = -1;
	}
}

void launchFirework() {
	lastLaunch = millis();
	startSnake(&f, (uint16_t)random(0, 65535), random(3, 5), MODE_BURST, start_brightness);
}

void launchFirework(uint16_t hue) {
	lastLaunch = millis();
	startSnake(&f, hue, random(3, 5), MODE_BURST, start_brightness);
}

void decayPixels() {
	for (int i=0; i<NUMPIXELS; i++) {
		pixelStrip[i].val *= decayFactor;
	}
}

void writeHeadPixels() {
	int nrmPos = 0;
	for (int i=0; i<NUMHEADS; i++) {
		if (heads[i].state == 0) {
			continue;
		}
		nrmPos = map(heads[i].positionOnEdge, 0, EDGE_SIZE, 0, 14);
		heads[i].e->pixels[ heads[i].e->direction * nrmPos ].hue = heads[i].hue;
		heads[i].e->pixels[ heads[i].e->direction * nrmPos ].sat = heads[i].sat;
		heads[i].e->pixels[ heads[i].e->direction * nrmPos ].val = heads[i].brightness;
	}
}

void renderPixels() {
	strip.clear();
	strip.fill(bg, 0, NUMPIXELS);
	for (int i=0; i<NUMPIXELS; i++) {
		if (pixelStrip[i].val > 0) {
			strip.setPixelColor(i, strip.ColorHSV(pixelStrip[i].hue, pixelStrip[i].sat, pixelStrip[i].val));
		}
	}
	strip.show();
}

void onMqttMessage(int messageSize) {
	char msgBuffer[100];

	if (messageSize > 99) {
		Serial.println("Rejecting incoming message as it is too long");
		return;
	}

	int charCounter = 0;
	while (mqttClient.available()) {
		msgBuffer[charCounter++] = (char)mqttClient.read();
	}

	Serial.print("Got MQTT message: ");
	Serial.println(msgBuffer);

	deserializeJson(jsonBuffer, msgBuffer);

	if (jsonBuffer.containsKey("action")) {
		const char *action = (const char *)jsonBuffer["action"];

		if (strcmp(action, "firework") == 0) {
			Serial.println("MQTT: Launching firework");
			if (jsonBuffer.containsKey("hue")) {
				launchFirework((uint16_t)jsonBuffer["hue"]);
			} else {
				launchFirework();
			}

		} else if (strcmp(action, "fireworkTick") == 0) {
			Serial.println("MQTT: Updating firework tick");
			if (jsonBuffer.containsKey("time")) {
				fireworkTick = (int)jsonBuffer["time"];
			}

		} else if (strcmp(action, "background") == 0) {
			Serial.println("MQTT: Setting background colour");
			if (jsonBuffer.containsKey("r") && jsonBuffer.containsKey("g") && jsonBuffer.containsKey("b")) {
				bg = strip.Color((uint8_t)jsonBuffer["r"], (uint8_t)jsonBuffer["g"], (uint8_t)jsonBuffer["b"]);
			}

		} else if (strcmp(action, "brightness") == 0) {
			Serial.println("MQTT: Setting starting brightness");
			if (jsonBuffer.containsKey("amount")) {
				start_brightness = (uint8_t)jsonBuffer["amount"];
			}

		} else if (strcmp(action, "snake") == 0) {
			Serial.println("MQTT: Changing snake");
			if (jsonBuffer.containsKey("enabled")) {
				if ((bool)jsonBuffer["enabled"]) {
					enableSnake();
				} else {
					disableSnake();
				}
			}

		} else {
			Serial.print("Unknown MQTT action: ");
			Serial.println(action);
		}
	}
}

void setupWifi() {
	int status = WL_IDLE_STATUS;     // the WiFi radio's status

	WiFi.setPins(8,7,4,2);

	// check for the presence of the shield:
	if (WiFi.status() == WL_NO_SHIELD) {
		Serial.println("WiFi shield not present");
		// don't continue:
		while (true);
	}

	// attempt to connect to WiFi network:
	while ( status != WL_CONNECTED) {
		status = WiFi.begin(wifi_ssid, wifi_pass);
		// wait 5 seconds for connection:
		delay(5000);
	}

	Serial.println("Network connected");

	if (!mqttClient.connect("10.1.0.1", 1883)) {
		Serial.println("MQTT connection failed");
		while(true);
	}

	mqttClient.onMessage(onMqttMessage);
	mqttClient.subscribe("/ledwall/1/test");
	Serial.println("MQTT connected");
}

void setup() {
	Serial.begin(115200);
	Serial.println("Start...");

	delay(3000);

	Serial.println("Connecting to wifi...");

	setupWifi();

	Serial.println("Initialising LED strip...");

	randomSeed(analogRead(0));
	initLattice();
	strip.begin();
	strip.show();

	// sercom'd SPI port setup
	pinPeripheral(11, PIO_SERCOM);
	pinPeripheral(12, PIO_SERCOM);
	pinPeripheral(13, PIO_SERCOM);

	for (int i=0; i<NUMHEADS; i++) {
		heads[i].state = 0;
	}

	for (int i=0; i<NUMPIXELS; i++) {
		pixelStrip[i].hue = 0;
		pixelStrip[i].sat = 0;
		pixelStrip[i].val = 0;
	}
	loopTime = millis();

	Serial.println("Starting snake...");

	enableSnake();
}

void loop() {

	decayPixels();
	advance();
	writeHeadPixels();
	renderPixels();

	mqttClient.poll();

	// At least 15 milliseconds between frames
	timeToSleep = 15 - (millis() - loopTime);
	if (timeToSleep > 0) {
		delay(timeToSleep);
	}
	loopTime = millis();

}

