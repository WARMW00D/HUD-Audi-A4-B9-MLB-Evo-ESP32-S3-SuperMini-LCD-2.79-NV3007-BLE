/* Standalone display test. Open this sketch separately in Arduino IDE. */
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static Arduino_HWSPI bus(7 /* DC */, 2 /* CS */, 4 /* SCK */, 5 /* MOSI */, -1);
static Arduino_NV3007 panel(&bus, 6 /* RST */, 1 /* landscape */, false,
    142, 428, 12, 0, 14, 0,
    nv3007_279_init_operations, sizeof(nv3007_279_init_operations));

void setup() {
    Serial.begin(115200);
    delay(1000);
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);
    Serial.println("NV3007 2.79 test: SCK=4 MOSI=5 CS=2 DC=7 RST=6 BL=1");
    if (!panel.begin(10000000)) {
        Serial.println("ERROR: SPI begin failed.");
        while (true) delay(1000);
    }
    panel.invertDisplay(false);
    Serial.printf("Init commands sent; size=%dx%d (no display readback).\n",
        panel.width(), panel.height());
}

void loop() {
    static const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    static const char *names[] = {"RED", "GREEN", "BLUE", "WHITE"};
    static unsigned i = 0;
    panel.fillScreen(colors[i]);
    Serial.println(names[i]);
    i = (i + 1) % 4;
    delay(1000);
}
