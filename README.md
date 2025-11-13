# tenergy32hub_rain_sensor (โปรเจคตัวอย่าง)

เอกสารฉบับย่อนี้อธิบายการทำงานของโปรแกรมตัวอย่างในโฟลเดอร์ `src/main.cpp` ซึ่งออกแบบมาร่วมกับไลบรารี `tenergy32hub` เพื่ออ่านค่า rain sensor ผ่าน Modbus RTU และเก็บสถานะล่าสุดอย่างปลอดภัยด้วย LittleFS

เนื้อหาสำคัญ
- ภาษา/แพลตฟอร์ม: Arduino (ESP32) บน PlatformIO
- ไลบรารีหลักที่ใช้: `tenergy32hub`, `ModbusMaster`, `LittleFS`

ภาพรวมการทำงาน
1. เริ่มต้นฮาร์ดแวร์
	- `Serial.begin(115200)` สำหรับ debug
	- เรียก `mcu.begin()` จาก `tenergy32hub` เพื่อเริ่มบอร์ดและอุปกรณ์บนบอร์ด (OLED, buzzer, LED ฯลฯ)
	- ตั้งค่า RS485 (UART2) ด้วยพิน RX=27, TX=26
	- พยายาม mount `LittleFS` เพื่อใช้เก็บสถานะถาวร (ถ้า mount ล้มเหลว โค้ดพยายามฟอร์แมตแล้ว mount ใหม่)

2. Persistence (เก็บสถานะ)
	- ไฟล์ persist อยู่ที่ LittleFS path: `/memory_spifs.json`
	- รูปแบบไฟล์: JSON-like แบบเรียบง่าย
	  {"stored_total":<uint32>,"last_sensor_reading":<uint32>,"timestamp":<uint32>,"crc":<uint16>}
	  - `stored_total` เก็บยอดสะสมของปริมาณฝน (หน่วยเป็น mm * 10)
	  - `last_sensor_reading` เก็บค่าดิบล่าสุดที่อ่านจากเซนเซอร์ (mm * 10)
	  - `timestamp` เป็นค่า `millis()` ขณะ persist
	  - `crc` เป็น checksum แบบง่ายสำหรับยืนยันความถูกต้อง
	- การออกแบบ persistence
	  - โหลด persisted state ตอน startup (ถ้ามีและ CRC ถูกต้อง)
	  - เมื่ออ่านค่าเซนเซอร์ใหม่ โค้ดคำนวณ delta ระหว่างค่าใหม่กับ `last_sensor_reading` และเพิ่มค่า delta ให้ `stored_total` เพื่อรักษายอดสะสมของระบบ
	  - หากพบว่า `sensorRaw < last_sensor_reading` แสดงว่าเซนเซอร์อาจถูกรีเซ็ต (power loss) — โค้ดจะเพิ่มค่า `sensorRaw` ลงใน `stored_total` (treat as new accumulation) เพื่อไม่ให้สูญเสียยอดสะสมที่ระบบเก็บไว้
	  - เก็บค่าลง LittleFS เฉพาะเมื่อมีการเปลี่ยนแปลงที่สำคัญ (>= `writeThresholdUnits`) หรือเมื่อเวลาผ่านไปนานกว่า `writeIntervalMs` เพื่อถนอมรอบการเขียนแฟลช

3. การอ่าน Modbus (rain sensor)
	- ใช้ไลบรารี `ModbusMaster` และ `modbus.readHoldingRegisters(0x0001, 1)` เพื่ออ่านค่า holding register ที่ address `0x0001` (1 word)
	- เซนเซอร์จะส่งค่าเป็นจำนวนหน่วย `mm * 10` (ตัวอย่าง 25 = 2.5 mm)
	- ค่าที่อ่านได้จะถูกแปลงเป็น `sensorRaw` (uint32) และ `_rainAmount` (float = sensorRaw / 10.0)

4. ฟังก์ชัน `resetRainsensor(reg, value)`
	- เขียนค่าแบบ single register (Modbus function 0x06) โดยใช้ `modbus.writeSingleRegister(reg, value)`
	- พารามิเตอร์เริ่มต้น: `reg = 0x0000`, `value = 0`
	- ถ้าเขียนสำเร็จ ฟังก์ชันจะอัปเดต `last_sensor_reading` ให้ตรงกับค่าที่เขียน และพยายาม `savePersist()` เพื่อให้สถานะภายในระบบสอดคล้องกับเซนเซอร์
	- หมายเหตุสำคัญ: อย่าลืมตรวจสอบว่าเครื่องมือที่ใช้ส่งคำสั่ง Modbus (เช่น Modbus Poll) นับ address เริ่มต้นที่ 0 หรือ 1 — อาจต้อง +1/-1 ขึ้นกับเครื่องมือ

5. Watchdog
	- ใช้ `esp_task_wdt` โดยตั้ง timeout = 10 วินาที และเรียก `esp_task_wdt_reset()` ใน loop เพื่อป้องกันการค้าง

6. ค่าเริ่มต้นและพารามิเตอร์ที่ปรับได้
	- `writeThresholdUnits = 10`  => 10 units = 1.0 mm (กำหนดว่าต้องมี delta เท่าไรจึงจะ persist)
	- `writeIntervalMs = 60000` => 60 วินาที (persist ทุก 60s หากไม่มี delta มากพอ)

7. ข้อควรระวังและข้อเสนอแนะ
	- หาก LittleFS มี corruption โค้ดจะพยายามฟอร์แมต (ลบข้อมูลทุกอย่างใน partition) และสร้างไฟล์ persist ใหม่ ถ้าต้องการเก็บไฟล์เดิมไว้ ต้องสำรองก่อนฟอร์แมต
	- หากฮาร์ดแวร์ RS485 ของคุณต้องการสลับโหมดส่ง/รับ (DE/RE) เพิ่มพินควบคุมและสลับก่อน/หลังการส่งข้อมูล Modbus เพื่อป้องกันการชนบนบัส
	- หากต้องการเขียนบ่อย (เช่น ทุกวินาที) ให้พิจารณาใช้ external FRAM หรือ SD card แทน LittleFS เพื่อหลีกเลี่ยงการสึกหรอของ flash
	- ปรับ threshold/interval ให้สอดคล้องกับความละเอียดที่ต้องการและอายุการใช้งานของแฟลช

8. คำสั่งใช้งานและการทดสอบ
	- คอมไพล์และอัปโหลดด้วย PlatformIO (ตัวอย่าง):
	  - เปิด PlatformIO และรัน Build/Upload ตามปกติ
	- ดูข้อความ Debug ผ่าน Serial Monitor ที่ 115200 bps
	- หากต้องการรีเซ็ตค่าเซนเซอร์ด้วยมือ (จาก code) ให้เรียก:
	  - `resetRainsensor(0x0000, 0);`  // หรือ address/value ตามที่ต้องการ

ไฟล์ที่สำคัญในโปรเจค
- `src/main.cpp` — โค้ดหลักที่อ่าน/คำนวณ/เก็บสถานะ rain sensor
- `data/memory_spifs.json` — ค่าเริ่มต้นสำหรับ LittleFS image (ไฟล์นี้จะถูกรวมเมื่ออัพโหลด filesystem)
- `lib/tenergy32hub` — ไลบรารีบอร์ด (ควบคุม OLED, LED, buzzer, switches)

ถ้าต้องการให้ผมปรับ README เพิ่มเติมหรือเพิ่มตัวอย่างการใช้งาน (เช่น ฟังก์ชัน Serial command สำหรับสั่ง reset, เพิ่มตัวเลือกใช้ `Preferences` แทน `LittleFS`, หรือเพิ่ม logging แบบละเอียด) บอกมาได้เลยครับ
