//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2017-10-24 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2018-09-29 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

//#define USE_LCD
//#define LCD_ADDRESS 0x27


#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <MultiChannelDevice.h>

#ifdef USE_LCD
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(LCD_ADDRESS, 20, 4);
#endif


// Arduino pin for the config button
#define CONFIG_BUTTON_PIN  8
#define LED_PIN            4
#define PH_PIN             14   //Analogue output PH sensor
#define O_PIN              15   //Analogue output Oxygene sensor 
#define T_PIN              16   //Temperatur from PH sensorboard


// number of available peers per channel
#define PEERS_PER_CHANNEL  6


//seconds between sending messages
#define MSG_INTERVAL       180


// all library classes are placed in the namespace 'as'
using namespace as;


// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0x69, 0x02, 0x01},          // Device ID           //69 02 01  my number
  "PHOSENS001",                // Device Serial       //PHOSENS001
  {0x69, 0x02},                // Device Model        //69 02     my sensor model
  0x10,                        // Firmware Version    //V1.0
  0x53,                        // Device Type         //??
  {0x01, 0x01}                 // Info Bytes          //??
};


/**
   Configure the used hardware
*/
typedef AvrSPI<10, 11, 12, 13> SPIType;
typedef Radio<SPIType, 2> RadioType;
typedef StatusLed<LED_PIN> LedType;
typedef AskSin<LedType, BatterySensor, RadioType> BaseHal;
class Hal : public BaseHal {
  public:
    void init (const HMID& id) {
      BaseHal::init(id);
      // measure battery every 1h
      battery.init(seconds2ticks(60UL*60),sysclock);
      battery.low(22);
      battery.critical(19);
    }
    
    bool runready () {
      return sysclock.runready() || BaseHal::runready();
    }
} hal;









class MeasureEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, int16_t temp, uint8_t oxygene, uint8_t PHvalue, bool batlow) {
      uint8_t t1 = (temp >> 8) & 0x7f;
      uint8_t t2 = temp & 0xff;
      if ( batlow == true ) {
        t1 |= 0x80; // set bat low bit
      }
      Message::init(0xc, msgcnt, 0x70, (msgcnt % 20 == 1) ? BIDI : BCAST, t1, t2);
      pload[0] = oxygene;
      pload[1] = PHvalue;
    }
};


class PHChannel : public Channel<Hal, List1, EmptyList, List4, PEERS_PER_CHANNEL, List0>, public Alarm {

    MeasureEventMsg msg;
    int16_t         temp;
    uint8_t         oxygene;
    uint8_t         PHvalue;
    uint16_t        millis;

  public:
    PHChannel () : Channel(), Alarm(5), temp(0), oxygene(0), PHvalue(0), millis(0) {}
    virtual ~PHChannel () {}
    
    // here we do the measurement
    void measure () {
      DPRINT("Measure...\n");
      
    //O
      oxygene = 0;

    //PH   
      int measuringVal = 0;
      for (int i=0; i<5; i++){
        measuringVal = measuringVal + analogRead(PH_PIN);
      }          
      double vltValue = 5/1024.0 * (measuringVal/5);
      float P0 = 7 + ((2.5 - vltValue) / 0.18);    
      PHvalue = P0;

    //T
      temp = 10.0;
      
      DPRINT("T/O/PH = " + String(temp)+"/"+ String(oxygene) +"/"+ String(PHvalue) + "\n");
    }
    
    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      uint8_t msgcnt = device().nextcount();
      // reactivate for next measure
      tick = delay();
      clock.add(*this);
      measure();

      msg.init(msgcnt, temp, oxygene, PHvalue, device().battery().low());
      device().sendPeerEvent(msg, *this);
    }

    uint32_t delay () {
      return seconds2ticks(MSG_INTERVAL);
    }
    void setup(Device<Hal, List0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);
      sysclock.add(*this);
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      return 0;
    }
};


typedef MultiChannelDevice<Hal, PHChannel, 1> PHOsensor;
PHOsensor sdev(devinfo, 0x20);
ConfigButton<PHOsensor> cfgBtn(sdev);

void setup() {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  
#ifdef USE_LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("HB-UNI-SEN-PHO");
  lcd.setCursor(5, 1);
  lcd.print((char*)serial);
  HMID temp;
  sdev.getDeviceID(temp);
  lcd.setCursor(7, 2);
  lcd.print(temp, HEX);
  sdev.getDeviceID(PHvalue);
  lcd.setCursor(7, 3);
  lcd.print(PHvalue, HEX);
#endif

  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
  
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    if ( hal.battery.critical() ) {
      hal.activity.sleepForever(hal);
    }
    hal.activity.savePower<Sleep<>>(hal);
  }
}
