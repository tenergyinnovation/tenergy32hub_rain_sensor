/***********************************************************************
 * Project      :     tenergy32hub_rain_sensor
 * Description  :     Template coding for tenergy32hub on vscode with platformIO
 * Hardware     :     tenergy32hub
 * Author       :     Tenergy Innovation Co., Ltd.
 * Date         :     27/04/2025
 * Revision     :     1.0
 * Rev1.0       :     Original
 * website      :     http://www.tenergyinnovation.co.th
 * Email        :     uten.boonliam@tenergyinnovation.co.th
 * TEL          :     +66 89-140-7205
 ***********************************************************************/
#include <Arduino.h>
#include <tenergy32hub.h>
#include <esp_task_wdt.h>
#include <esp_system.h> // สำหรับ esp_read_mac
#include <ModbusMaster.h>
#include <LittleFS.h>

/**************************************/
/*          Firmware Version          */
/**************************************/
String version = "0.1";

/**************************************/
/*          Header project            */
/**************************************/
void header_print(void)
{
    Serial.printf("\r\n***********************************************************************\r\n");
    Serial.printf("* Project      :     tenergy32hub_rain_sensor\r\n");
    Serial.printf("* Description  :     Template coding for tenergy32hub on vscode with platformIO\r\n");
    Serial.printf("* Hardware     :     tenergy32hub\r\n");
    Serial.printf("* Author       :     Tenergy Innovation Co., Ltd.\r\n");
    Serial.printf("* Date         :     04/07/2022\r\n");
    Serial.printf("* Revision     :     %s\r\n", version);
    Serial.printf("* Rev1.0       :     Origital\r\n");
    Serial.printf("* website      :     http://www.tenergyinnovation.co.th\r\n");
    Serial.printf("* Email        :     uten.boonliam@tenergyinnovation.co.th\r\n");
    Serial.printf("* TEL          :     +66 89-140-7205\r\n");
    Serial.printf("***********************************************************************/\r\n");
}

/**************************************/
/*        define object variable      */
/**************************************/
Tenergy32Hub mcu;
ModbusMaster modbus;     // สร้างอ็อบเจ็กต์ ModbusMaster สำหรับการสื่อสาร Modbus
HardwareSerial rs485(2); // ใช้ UART2 สำหรับ RS485

/**************************************/
/*            GPIO define             */
/**************************************/

/**************************************/
/*       Constand define value        */
/**************************************/
// 10 seconds WDT
#define WDT_TIMEOUT 10

// Modbus Slave ID
#define WIND_DIRECTION_SENSOR_ID 1
#define WIND_SPEED_SENSOR_ID 2
#define RAIN_SENSOR_ID 3

// GPIO define
#define PIN_RX 27 // RX pin for RS485
#define PIN_TX 26 // TX pin for RS485

// Persistence (LittleFS) configuration
#define PERSIST_FILE "/memory_spifs.json"

// persist units: stored in integer = mm * 10
static uint32_t stored_total = 0;        // cumulative total kept by system (mm*10)
static uint32_t last_sensor_reading = 0; // last raw value read from sensor (mm*10)
static uint32_t last_persist_time = 0;   // millis() of last persist

// Parameters (defaults as requested)
const uint32_t writeThresholdUnits = 10;      // units (mm*10) => 10 units = 1.0 mm
const uint32_t writeIntervalMs = 60UL * 1000; // 60 seconds

/**************************************/
/*       eeprom address define        */
/**************************************/

/**************************************/
/*        define global variable      */
/**************************************/

// ตัวแปรสำหรับเก็บชื่อ unitName
String unitName = "";

/**************************************/
/*           define function          */
/**************************************/
float readRainsensor();
bool resetRainsensor();
bool resetRainAccumulation_SPIFS();

static uint16_t calcChecksum(uint32_t a, uint32_t b, uint32_t c);
static bool loadPersist();
static bool savePersist();

/***********************************************************************
 * FUNCTION:    getUnitNameFromMac
 * DESCRIPTION: สร้างชื่อ unitName จาก MAC Address (6 ตัวหลัง)
 * RETURNED:    String ชื่อบอร์ด tenergy32hub-xxxxxx
 ***********************************************************************/
String getUnitNameFromMac()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[7];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return "tenergy32hub-" + String(macStr);
}

/***********************************************************************
 * FUNCTION:    setup
 * DESCRIPTION: setup process
 * PARAMETERS:  nothing
 * RETURNED:    nothing
 ***********************************************************************/
void setup()
{
    // Initialize serial communication and print the header
    Serial.begin(115200);
    header_print();

    // Initialize and enable the watchdog with a 10-second timeout.
    esp_task_wdt_init(WDT_TIMEOUT, true); // true resets the CPU on WDT timeout
    esp_task_wdt_add(NULL);               // Add current task to watchdog monitoring

    mcu.begin();
    mcu.displayOLEDInfo();
    vTaskDelay(1000);

    // สร้าง unitName จาก MAC Address
    unitName = getUnitNameFromMac();

    // แสดงชื่อ unitName บน Serial และ OLED
    Serial.printf("unitName: %s\r\n", unitName.c_str());
    mcu.displayOLED(unitName.c_str());

    // ตั้งค่า RS485
    Serial.println("Setting RS485 pins...");
    rs485.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);

    // Initialize LittleFS and show persisted file if present
    bool lfs_ok = false;
    if (LittleFS.begin())
    {
        lfs_ok = true;
        Serial.println("LittleFS mounted");
    }
    else
    {
        Serial.println("LittleFS mount failed, attempting to format and retry...");
        // try to format and mount (some LittleFS variants accept begin(true) to format)
        if (LittleFS.begin(true))
        {
            lfs_ok = true;
            Serial.println("LittleFS formatted and mounted");
        }
        else
        {
            Serial.println("LittleFS mount and format failed");
        }
    }

    if (lfs_ok)
    {
        if (loadPersist())
        {
            Serial.println("Loaded persisted rain state:");
            Serial.printf(" stored_total=%lu (%.1f mm)\r\n", stored_total, stored_total / 10.0);
            Serial.printf(" last_sensor_reading=%lu (%.1f mm)\r\n", last_sensor_reading, last_sensor_reading / 10.0);
            Serial.printf(" last_persist_time=%lu\r\n", last_persist_time); // ค่าเวลา
            // show brief info on OLED
            mcu.clearOLED();
            mcu.displayOLEDLines("Persist Loaded", (String("Total:") + String(stored_total / 10.0) + " mm").c_str(), (String("LastRaw:") + String(last_sensor_reading / 10.0) + " mm").c_str(), " ");
        }
        else
        {
            Serial.println("No valid persisted rain state found (starting fresh)");
            mcu.clearOLED();
            mcu.displayOLEDLines("Persist", "No saved state", "Starting fresh", " ");
            // ensure file exists by saving initial state
            if (savePersist())
                Serial.println("Wrote initial persist file");
            else
                Serial.println("Failed to write initial persist file");
        }
    }

    // Initialize Modbus master once (use rs485 Serial)
    modbus.begin(RAIN_SENSOR_ID, rs485);
}

/***********************************************************************
 * FUNCTION:    loop
 * DESCRIPTION: loop process
 * PARAMETERS:  nothing
 * RETURNED:    nothing
 ***********************************************************************/
void loop()
{
    float _rainAmount = readRainsensor();
    static uint16_t _counter = 0;
    Serial.printf("Rain Amount[%d]: %.1f mm/h \t\t total=%.1f mm\r\n", _counter++, _rainAmount, stored_total / 10.0);

    /* กดสวิตซ์ SW1 เพื่อรีเซ็ตการสะสมของเซ็นเซอร์ฝน */
    if(mcu.readSW1())
    {
        mcu.beep(1, 50);
        Serial.println("SW1 pressed: resetting rain sensor accumulation");
        if (resetRainsensor())
        {
            Serial.println("Rain sensor accumulation reset command sent");
        }
        else
        {
            Serial.println("Failed to send reset command to rain sensor");
        }

    }

    /* กดสวิตซ์ SW2 เพื่อรีเซ็ตการสะสมที่เก็บใน SPIFS */
    if(mcu.readSW2())
    {
        mcu.beep(2, 50);
        Serial.println("SW2 pressed: resetting stored rain accumulation in SPIFS");
        if (resetRainAccumulation_SPIFS())
        {
            Serial.println("Stored rain accumulation reset in SPIFS");
        }
        else
        {
            Serial.println("Failed to reset stored rain accumulation in SPIFS");
        }
    }



    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/***********************************************************************
 * FUNCTION:    readRainsensor
 * DESCRIPTION: อ่านค่าจาก Wind Direction Sensor ผ่าน Modbus RTU
 * PARAMETERS:  nothing
 * RETURNED:    float ค่าฝนที่อ่านได้ (mm/h)
 ***********************************************************************/
float readRainsensor()
{
    float _rainAmount = 0.0;

    // Read 1 holding register at 0x0001 (sensor value = mm * 10)
    uint8_t _result = modbus.readHoldingRegisters(0x0001, 1);
    String statusLine;

    if (_result == modbus.ku8MBSuccess)
    {
        uint32_t sensorRaw = (uint32_t)modbus.getResponseBuffer(0);
        _rainAmount = sensorRaw / 10.0;
        // Serial.printf("Rain sensor raw: %lu -> %.1f mm\n", sensorRaw, _rainAmount);

        // compute delta according to persisted last_sensor_reading
        uint32_t delta = 0;
        if (sensorRaw >= last_sensor_reading)
        {
            delta = sensorRaw - last_sensor_reading;
        }
        else
        {
            // sensor reset detected (sensor lost its internal accumulation)
            Serial.println("Sensor reset detected (sensorRaw < last_sensor_reading)");
            // treat current sensorRaw as new accumulated since sensor reset
            delta = sensorRaw;
        }

        if (delta > 0)
        {
            stored_total += delta;
            Serial.printf("Delta: %lu units -> add to stored_total -> total=%lu (%.1f mm)\n", delta, stored_total, stored_total / 10.0);
        }
        else
        {
            // Serial.println("No delta to add");
        }

        // update last_sensor_reading
        last_sensor_reading = sensorRaw;

        // persist if threshold or interval reached
        uint32_t now = millis();
        if (delta >= writeThresholdUnits || (now - last_persist_time) >= writeIntervalMs)
        {
            if (savePersist())
                Serial.println("Persisted rain state to LittleFS");
            else
                Serial.println("Failed to persist rain state");
        }
    }
    else
    {
        Serial.print("Rain Sensor Error: ");
        switch (_result)
        {
        case ModbusMaster::ku8MBIllegalFunction:
            Serial.println("Illegal Function");
            break;
        case ModbusMaster::ku8MBIllegalDataAddress:
            Serial.println("Illegal Data Address");
            break;
        case ModbusMaster::ku8MBIllegalDataValue:
            Serial.println("Illegal Data Value");
            break;
        case ModbusMaster::ku8MBSlaveDeviceFailure:
            Serial.println("Slave Device Failure");
            break;
        case ModbusMaster::ku8MBInvalidSlaveID:
            Serial.println("Invalid Slave ID");
            break;
        case ModbusMaster::ku8MBInvalidFunction:
            Serial.println("Invalid Function");
            break;
        case ModbusMaster::ku8MBResponseTimedOut:
            Serial.println("Response Timed Out");
            break;
        case ModbusMaster::ku8MBInvalidCRC:
            Serial.println("Invalid CRC");
            break;
        default:
            Serial.println("Unknown error");
            break;
        }
    }

    return _rainAmount;
}

/***********************************************************************
 * FUNCTION:    resetRainsensor
 * DESCRIPTION: Write a single register to the rain sensor (Modbus function 0x06)
 * PARAMETERS:  none
 * RETURNED:    true on success, false on failure
 ***********************************************************************/
bool resetRainsensor()
{
    uint16_t _reg = 0x0000; 
    uint16_t _value = 90;
    Serial.printf("ResetRainSensor: write reg 0x%04X <- %u\n", _reg, _value);
    uint8_t res = modbus.writeSingleRegister(_reg, _value);
    if (res == modbus.ku8MBSuccess)
    {
        Serial.println("Modbus writeSingleRegister success");
        // Update local cached value so logic stays consistent after manual write
        // If user wrote the sensor accumulation register, reflect it locally
        if (_reg == 0x0001 || _reg == 0x0000)
        {
            last_sensor_reading = _value;
            // Do not modify stored_total here: stored_total represents system-held cumulative total.
            // Persist updated last_sensor_reading so subsequent reads behave correctly.
            if (savePersist())
                Serial.println("Persisted state after manual reset");
            else
                Serial.println("Failed to persist state after manual reset");
        }
        return true;
    }
    else
    {
        Serial.printf("Modbus writeSingleRegister failed, code=%u\n", res);
        return false;
    }
}

/***********************************************************************
 * FUNCTION:    resetRainAccumulation_SPIFS
 * DESCRIPTION: Reset rain accumulation stored in SPIFS (stored_total and last_sensor_reading)
 * PARAMETERS:  none
 * RETURNED:    true on success, false on failure
 ***********************************************************************/
bool resetRainAccumulation_SPIFS()
{
    stored_total = 0;
    last_sensor_reading = 0;
    if (savePersist())
    {
        Serial.println("Persisted state after SPIFS reset");
        return true;
    }
    else
    {
        Serial.println("Failed to persist state after SPIFS reset");
        return false;
    }
}

/***********************************************************************
 * FUNCTION:    calcChecksum
 * DESCRIPTION: calculate simple checksum over three uint32_t values
 * PARAMETERS:  a, b, c - values to include in checksum
 * RETURNED:    16-bit checksum
 ***********************************************************************/
static uint16_t calcChecksum(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t sum = 0;
    sum += (a & 0xFFFF) + ((a >> 16) & 0xFFFF);
    sum += (b & 0xFFFF) + ((b >> 16) & 0xFFFF);
    sum += (c & 0xFFFF) + ((c >> 16) & 0xFFFF);
    return (uint16_t)(sum & 0xFFFF);
}

/***********************************************************************
 * FUNCTION:    loadPersist
 * DESCRIPTION: Load persisted JSON-like file from LittleFS
 * PARAMETERS:  none
 * RETURNED:    true if loaded successfully, false otherwise
 ***********************************************************************/
static bool loadPersist()
{
    if (!LittleFS.exists(PERSIST_FILE))
        return false;

    File f = LittleFS.open(PERSIST_FILE, "r");
    if (!f)
        return false;

    String s = f.readString();
    f.close();

    // Expected simple JSON format: {"stored_total":123,"last_sensor_reading":45,"timestamp":678,"crc":901}
    uint32_t st = 0, lr = 0, ts = 0;
    unsigned int crc = 0;
    int parsed = sscanf(s.c_str(), "{\"stored_total\":%lu,\"last_sensor_reading\":%lu,\"timestamp\":%lu,\"crc\":%u}", &st, &lr, &ts, &crc);
    if (parsed == 4)
    {
        uint16_t c = calcChecksum(st, lr, ts);
        if (c == (uint16_t)crc)
        {
            stored_total = st;
            last_sensor_reading = lr;
            last_persist_time = ts;
            return true;
        }
        else
        {
            Serial.println("Persist CRC mismatch, ignoring persisted data");
            return false;
        }
    }

    Serial.println("Persist file parse failed");
    return false;
}

/***********************************************************************
 * FUNCTION:    savePersist
 * DESCRIPTION: Save persisted state to LittleFS (atomic-ish by writing temp then rename)
 * PARAMETERS:  none
 * RETURNED:    true if saved successfully, false otherwise
 ***********************************************************************/
static bool savePersist()
{
    uint32_t ts = millis();
    uint16_t crc = calcChecksum(stored_total, last_sensor_reading, ts);

    String s = String("{\"stored_total\":") + String(stored_total) + String(",\"last_sensor_reading\":") + String(last_sensor_reading) + String(",\"timestamp\":") + String(ts) + String(",\"crc\":") + String(crc) + String("}");

    // write to temp file then replace
    const char *tmp = "/.tmp_persist";
    File f = LittleFS.open(tmp, "w");
    if (!f)
        return false;
    f.print(s);
    f.close();

    // remove old and rename
    if (LittleFS.exists(PERSIST_FILE))
        LittleFS.remove(PERSIST_FILE);
    bool ok = LittleFS.rename(tmp, PERSIST_FILE);
    if (ok)
    {
        last_persist_time = ts;
    }
    else
    {
        // attempt write directly as fallback
        File f2 = LittleFS.open(PERSIST_FILE, "w");
        if (!f2)
            return false;
        f2.print(s);
        f2.close();
        last_persist_time = ts;
        ok = true;
    }
    return ok;
}
