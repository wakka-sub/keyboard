#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HID-Project.h>
#include <EEPROM.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>

// U8g2をページバッファモードで初期化
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0);

// --- 定数定義 ---
const uint8_t ENCODER_A_PIN = 0, ENCODER_B_PIN = 1, ENCODER_SW_PIN = A0;
const uint8_t KEY_PINS[] = {4, 5, 6, 7, 8, 9, 10, 16};
const uint8_t NUM_KEYS = sizeof(KEY_PINS) / sizeof(KEY_PINS[0]);
const unsigned long DEBOUNCE_TICKS = 5;
const int ENCODER_STEPS_PER_CLICK = 4;
const int EEPROM_ADDR = 0;
const uint16_t EEPROM_MAGIC = 0xADF1;
const int MAX_COMBO_KEYS = 4;

const uint8_t NUM_ENCODER_MAPS = 5;
const uint8_t NUM_TOTAL_MAPS = NUM_KEYS + NUM_ENCODER_MAPS;
const int ENCODER_CW_INDEX = NUM_KEYS;
const int ENCODER_CCW_INDEX = NUM_KEYS + 1;
const int ENCODER_SW_INDEX = NUM_KEYS + 2;
const int ENCODER_SW_CW_INDEX = NUM_KEYS + 3;
const int ENCODER_SW_CCW_INDEX = NUM_KEYS + 4;

// --- データ構造 ---
enum KeyType : uint8_t { NONE = 0, KEYBOARD = 1, CONSUMER = 2, COMMAND = 3 };
struct KeyMapping {
    KeyType type;
    uint16_t codes[MAX_COMBO_KEYS];
};
KeyMapping keyMap[NUM_TOTAL_MAPS];

// --- グローバル変数 ---
volatile bool currentKeyStates[NUM_KEYS] = {false};
bool lastKeyStates[NUM_KEYS] = {false};
volatile uint8_t debounceCounters[NUM_KEYS] = {0};

volatile bool currentEncoderSwState = false;
bool lastEncoderSwState = false;
volatile uint8_t encoderSwDebounceCounter = 0;

volatile long encoderCount = 0;
volatile uint8_t encoderState = 0;
long lastProcessedEncoderCount = 0;
volatile long encoderCountAtSwPress = 0;

enum KeyEventType : uint8_t { EVENT_NONE, EVENT_PRESS, EVENT_RELEASE };
volatile KeyEventType keyEvents[NUM_KEYS] = {EVENT_NONE};
volatile bool encoderSwEvent = false;
volatile int encoderSteps = 0;

bool uiNeedsUpdate = true;
bool isDrawing = false;

// --- UIおよび曲名スクロール関連 ---
constexpr size_t MAX_SONG_LEN = 64;
char     currentSongName[MAX_SONG_LEN] = "Waiting for the beat...";
bool     isPlaying = false;
const int DISPLAY_WIDTH = 128;
const int SONG_NAME_Y = 16;
const unsigned long SCROLL_INTERVAL = 33;
const int SCROLL_PIXELS = 1;
int      song_name_pixel_width = 0;
int      scroll_offset_x = 0;
unsigned long last_scroll_time = 0;

// --- 音楽再生の進捗情報に関する変数 ---
unsigned long currentPositionMs = 0;
unsigned long totalDurationMs = 0;
unsigned long lastInfoUpdateTimestamp = 0;
unsigned long lastPositionAtUpdate = 0;

// --- 関数のプロトタイプ宣言 ---
void loadConfig();
void saveConfig();
void handleEncoderISR();
void setup();
void loop();
void processEvents();
void scanKeysAndEncoder();
void handleSerialCommands();
void executeMapping(int mapIndex, bool pressed);
void updateUiState();
void drawScreenContent();
void setupTimerInterrupt();
int getFreeRam();
void formatTime(char* buffer, size_t bufferSize, unsigned long totalMilliseconds);

// --- 割り込みサービスルーチン (エンコーダー回転) ---
void handleEncoderISR() {
    const int8_t lookup_table[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    uint8_t currentState = (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    uint8_t index = (encoderState << 2) | currentState;
    int8_t direction = lookup_table[index];
    if (direction != 0) {
        encoderCount -= direction;
    }
    encoderState = currentState;
}

// --- 割り込みサービスルーチン (1msタイマー) ---
ISR(TIMER1_COMPA_vect) {
    scanKeysAndEncoder();
}

// --- セットアップ ---
void setup() {
    Serial.begin(115200);
    Wire.setClock(400000L);
    u8g2.begin();
    u8g2.enableUTF8Print();
    Keyboard.begin();
    Consumer.begin();

    loadConfig();

    for (int i = 0; i < NUM_KEYS; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);
    pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
    pinMode(ENCODER_A_PIN, INPUT_PULLUP);
    pinMode(ENCODER_B_PIN, INPUT_PULLUP);
    
    delay(2);
    
    encoderState = (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), handleEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), handleEncoderISR, CHANGE);

    u8g2.setFont(u8g2_font_6x10_tf);
    song_name_pixel_width = u8g2.getStrWidth(currentSongName);
    
    setupTimerInterrupt();

    uiNeedsUpdate = true;
}

// --- メインループ ---
void loop() {
    handleSerialCommands();
    processEvents();
    updateUiState();

    if (!isDrawing && uiNeedsUpdate) {
        u8g2.firstPage();
        isDrawing = true;
        uiNeedsUpdate = false;
    }

    if (isDrawing) {
        drawScreenContent();
        if (!u8g2.nextPage()) {
            isDrawing = false;
        }
    }
}

// UIの状態更新ロジック
void updateUiState() {
    bool needsRedraw = false;

    if (song_name_pixel_width > DISPLAY_WIDTH) {
        unsigned long current_time = millis();
        if (current_time - last_scroll_time > SCROLL_INTERVAL) {
            last_scroll_time = current_time;
            scroll_offset_x -= SCROLL_PIXELS;
            
            constexpr int gap = 40;
            if (scroll_offset_x <= (-song_name_pixel_width - gap)) {
                scroll_offset_x += (song_name_pixel_width + gap);
            }
            needsRedraw = true;
        }
    }

    if (isPlaying && totalDurationMs > 0) {
        currentPositionMs = lastPositionAtUpdate + (millis() - lastInfoUpdateTimestamp);
        if (currentPositionMs > totalDurationMs) {
            currentPositionMs = totalDurationMs;
        }
        needsRedraw = true;
    }
    
    if(needsRedraw){
        uiNeedsUpdate = true;
    }
}

// ミリ秒を MM:SS 形式の文字列に変換するヘルパー関数
void formatTime(char* buffer, size_t bufferSize, unsigned long totalMilliseconds) {
    if (bufferSize < 6) return;

    unsigned long totalSeconds = totalMilliseconds / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    snprintf(buffer, bufferSize, "%d:%02d", minutes, seconds);
}


// 画面の描画内容を定義する関数
void drawScreenContent() {
    constexpr int W = 128;
    constexpr int H = 64;
    
    u8g2.setFont(u8g2_font_6x10_tf);
    
    // --- 1. 曲名表示 (Y=16) ---
    u8g2.setFontPosBaseline();
    if (song_name_pixel_width > W) {
        constexpr int gap = 40;
        u8g2.drawStr(scroll_offset_x, SONG_NAME_Y, currentSongName);
        u8g2.drawStr(scroll_offset_x + song_name_pixel_width + gap, SONG_NAME_Y, currentSongName);
    } else {
        int16_t tw = song_name_pixel_width;
        u8g2.drawStr((W - tw) / 2, SONG_NAME_Y, currentSongName);
    }
    
    // --- 区切り線 ---
    // const int separatorY = 26; // ★ MODIFIED: Y座標を2px下に
    // u8g2.drawHLine(0, separatorY, W);


    // --- 2. プログレスバーと時間表示 (Y=40) ---
    if (totalDurationMs > 0) {
        const int progressAreaY = 40; // ★ MODIFIED: Y座標を2px下に
        const int barH = 4;
        const int textMargin = 5;

        u8g2.setFontPosCenter();
        
        char timeStr[6];
        char durationStr[6];
        
        formatTime(timeStr, sizeof(timeStr), currentPositionMs);
        formatTime(durationStr, sizeof(durationStr), totalDurationMs);

        int timeStrW = u8g2.getStrWidth(timeStr);
        int durationStrW = u8g2.getStrWidth(durationStr);

        u8g2.drawStr(0, progressAreaY, timeStr);
        u8g2.drawStr(W - durationStrW, progressAreaY, durationStr);

        int barX = timeStrW + textMargin;
        int barMaxW = W - timeStrW - durationStrW - (textMargin * 2);
        int barY = progressAreaY - (barH / 2);

        uint16_t progressW = (uint16_t)((float)currentPositionMs / totalDurationMs * barMaxW);
        
        if (barMaxW > 0) {
            u8g2.drawFrame(barX, barY, barMaxW, barH);
            u8g2.drawBox(barX, barY, progressW, barH);
        }
    }

    // --- 3. 操作アイコン表示 (Y=57) ---
    u8g2.setFontPosBaseline();
    constexpr int iconW = 14;
    constexpr int spacing = 22;
    const int cy      = 57; // このY座標は維持
    const int cx_play = W / 2;
    const int cx_prev = cx_play - (iconW + spacing);
    const int cx_next = cx_play + (iconW + spacing);

    { // Previous
        int barW = 2, barH = 10;
        u8g2.drawBox(cx_prev + iconW / 2 - barW, cy - barH / 2, barW, barH);
        u8g2.drawTriangle(cx_prev - iconW / 2 + 2, cy, cx_prev + iconW / 2 - barW - 2, cy - barH / 2, cx_prev + iconW / 2 - barW - 2, cy + barH / 2);
    }
    { // Play/Pause
        int barW = 3, barH = 12;
        if (isPlaying) {
            u8g2.drawBox(cx_play - barW, cy - barH / 2, barW, barH);
            u8g2.drawBox(cx_play + 2, cy - barH / 2, barW, barH);
        } else {
            u8g2.drawTriangle(cx_play - iconW / 2 + 2, cy - barH / 2, cx_play - iconW / 2 + 2, cy + barH / 2, cx_play + iconW / 2 - 2, cy);
        }
    }
    { // Next
        int barW = 2, barH = 10;
        u8g2.drawTriangle(cx_next + iconW / 2 - 2, cy, cx_next - iconW / 2 + barW + 2, cy - barH / 2, cx_next - iconW / 2 + barW + 2, cy + barH / 2);
        u8g2.drawBox(cx_next - iconW / 2, cy - barH / 2, barW, barH);
    }
}


// --- 以下の関数群は変更ありません ---

void setupTimerInterrupt() {
    cli();
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;
    OCR1A = 249;
    TCCR1B |= (1 << WGM12);
    TCCR1B |= (1 << CS11) | (1 << CS10);
    TIMSK1 |= (1 << OCIE1A);
    sei();
}

void scanKeysAndEncoder() {
    for (int i = 0; i < NUM_KEYS; i++) {
        bool reading = (digitalRead(KEY_PINS[i]) == LOW);
        if (reading != lastKeyStates[i]) {
            debounceCounters[i]++;
            if (debounceCounters[i] >= DEBOUNCE_TICKS) {
                lastKeyStates[i] = reading;
                currentKeyStates[i] = reading;
                keyEvents[i] = reading ? EVENT_PRESS : EVENT_RELEASE;
                debounceCounters[i] = 0;
            }
        } else {
            debounceCounters[i] = 0;
        }
    }

    bool swReading = (digitalRead(ENCODER_SW_PIN) == LOW);
    if (swReading != lastEncoderSwState) {
        encoderSwDebounceCounter++;
        if (encoderSwDebounceCounter >= DEBOUNCE_TICKS) {
            lastEncoderSwState = swReading;
            bool oldState = currentEncoderSwState;
            currentEncoderSwState = swReading;
            if (oldState != currentEncoderSwState) {
                if (currentEncoderSwState) {
                    noInterrupts();
                    encoderCountAtSwPress = encoderCount;
                    interrupts();
                } else {
                    noInterrupts();
                    long cntNow = encoderCount;
                    interrupts();
                    if (cntNow == encoderCountAtSwPress) {
                        encoderSwEvent = true;
                    }
                }
            }
            encoderSwDebounceCounter = 0;
        }
    } else {
        encoderSwDebounceCounter = 0;
    }
}

void processEvents() {
    for (int i = 0; i < NUM_KEYS; i++) {
        noInterrupts();
        KeyEventType event = keyEvents[i];
        keyEvents[i] = EVENT_NONE;
        interrupts();

        if (event == EVENT_PRESS) {
            executeMapping(i, true);
        } else if (event == EVENT_RELEASE) {
            executeMapping(i, false);
        }
    }

    if (encoderSwEvent) {
        executeMapping(ENCODER_SW_INDEX, true);
        executeMapping(ENCODER_SW_INDEX, false);
        encoderSwEvent = false;
    }

    noInterrupts();
    long c = encoderCount;
    interrupts();
    
    long d = c - lastProcessedEncoderCount;
    bool isSwPressed = currentEncoderSwState;

    if (d >= ENCODER_STEPS_PER_CLICK) {
        int steps = d / ENCODER_STEPS_PER_CLICK;
        int mapIndex = isSwPressed ? ENCODER_SW_CW_INDEX : ENCODER_CW_INDEX;
        for (int i = 0; i < steps; i++) executeMapping(mapIndex, true);
        lastProcessedEncoderCount += steps * ENCODER_STEPS_PER_CLICK;
    } else if (d <= -ENCODER_STEPS_PER_CLICK) {
        int steps = -d / ENCODER_STEPS_PER_CLICK;
        int mapIndex = isSwPressed ? ENCODER_SW_CCW_INDEX : ENCODER_CCW_INDEX;
        for (int i = 0; i < steps; i++) executeMapping(mapIndex, true);
        lastProcessedEncoderCount -= steps * ENCODER_STEPS_PER_CLICK;
    }
}

void executeMapping(int mapIndex, bool pressed) {
    KeyMapping mapping = keyMap[mapIndex];
    if (mapping.type == KeyType::NONE) return;

    if (mapping.type == KeyType::COMMAND) {
        if (pressed) {
            Serial.print("CMD:");
            Serial.println(mapIndex);
        }
        return;
    }
    
    if (mapIndex >= ENCODER_CW_INDEX && mapIndex <= ENCODER_SW_CCW_INDEX) {
        if (mapping.type == KeyType::CONSUMER) {
            if (mapping.codes[0] != 0) Consumer.write((ConsumerKeycode)mapping.codes[0]);
        } else if (mapping.type == KeyType::KEYBOARD) {
            for (int j = 0; j < MAX_COMBO_KEYS; j++) {
                if (mapping.codes[j] != 0) Keyboard.press((KeyboardKeycode)mapping.codes[j]);
            }
            Keyboard.releaseAll();
        }
        return;
    }
    
    for (int j = 0; j < MAX_COMBO_KEYS; j++) {
        if (mapping.codes[j] == 0) continue;
        if (pressed) {
            if (mapping.type == KeyType::KEYBOARD) Keyboard.press((KeyboardKeycode)mapping.codes[j]);
            else if (mapping.type == KeyType::CONSUMER) Consumer.press((ConsumerKeycode)mapping.codes[j]);
        } else {
            if (mapping.type == KeyType::KEYBOARD) Keyboard.release((KeyboardKeycode)mapping.codes[j]);
            else if (mapping.type == KeyType::CONSUMER) Consumer.release((ConsumerKeycode)mapping.codes[j]);
        }
    }
}

void handleSerialCommands() {
    if (Serial.available() > 0) {
        char line_buffer[256]; 
        int bytes_read = Serial.readBytesUntil('\n', line_buffer, sizeof(line_buffer) - 1);
        line_buffer[bytes_read] = '\0';

        char* command = line_buffer;
        while (*command == ' ' || *command == '\t' || *command == '\r') { command++; }

        if (strcmp(command, "GET_CONFIG") == 0) {
            Serial.print("CONFIG:");
            for (int i = 0; i < NUM_TOTAL_MAPS; i++) {
                Serial.print(keyMap[i].type); Serial.print(",");
                for (int j = 0; j < MAX_COMBO_KEYS; j++) {
                    Serial.print(keyMap[i].codes[j]);
                    if (j < MAX_COMBO_KEYS - 1) Serial.print(",");
                }
                if (i < NUM_TOTAL_MAPS - 1) Serial.print(",");
            }
            Serial.println();
        } else if (strncmp(command, "SET_CONFIG:", 11) == 0) {
            char* data_part = command + 11;
            char* p = strtok(data_part, ",");
            for (int i = 0; i < NUM_TOTAL_MAPS; i++) {
                if (p == nullptr) break;
                keyMap[i].type = (KeyType)atoi(p);
                for (int j = 0; j < MAX_COMBO_KEYS; j++) {
                    p = strtok(nullptr, ",");
                    if (p == nullptr) { keyMap[i].codes[j] = 0; }
                    else { keyMap[i].codes[j] = (uint16_t)atoi(p); }
                }
                if (i < NUM_TOTAL_MAPS -1) { p = strtok(nullptr, ","); }
            }
            saveConfig();
            Serial.println("OK");
        } else if (strcmp(command, "RESET_CONFIG") == 0) {
            EEPROM.put(EEPROM_ADDR, 0xFFFF);
            Serial.println("Config erased. Please reboot the device.");
        } else if (strcmp(command, "GET_STATS") == 0) {
            int totalRam = RAMSIZE;
            int freeRam = getFreeRam();
            int usedRam = totalRam - freeRam;
            int totalEeprom = EEPROM.length();
            int usedEeprom = sizeof(EEPROM_MAGIC) + sizeof(keyMap);
            int freeEeprom = totalEeprom - usedEeprom;
            Serial.print("SRAM: ");
            Serial.print(usedRam); Serial.print("/"); Serial.print(totalRam); Serial.print(" B");
            Serial.print(", EEPROM: ");
            Serial.print(usedEeprom); Serial.print("/"); Serial.print(totalEeprom); Serial.print(" B");
            Serial.println();
        } else if (strncmp(command, "SONG_INFO:", 10) == 0) {
            char* data = command + 10;
            
            char* p_title = strtok(data, ",");
            if (p_title != nullptr) {
                strncpy(currentSongName, p_title, MAX_SONG_LEN - 1);
                currentSongName[MAX_SONG_LEN - 1] = '\0';
                
                char* p_status = strtok(nullptr, ",");
                if (p_status != nullptr) { isPlaying = (atoi(p_status) != 0); }

                char* p_pos = strtok(nullptr, ",");
                if (p_pos != nullptr) { lastPositionAtUpdate = atol(p_pos); } else { lastPositionAtUpdate = 0; }

                char* p_dur = strtok(nullptr, ",");
                if (p_dur != nullptr) { totalDurationMs = atol(p_dur); } else { totalDurationMs = 0; }

                lastInfoUpdateTimestamp = millis();
                currentPositionMs = lastPositionAtUpdate;

                u8g2.setFont(u8g2_font_6x10_tf);
                song_name_pixel_width = u8g2.getStrWidth(currentSongName);
                
                if (song_name_pixel_width > DISPLAY_WIDTH) {
                    scroll_offset_x = 0;
                }
                
                uiNeedsUpdate = true;
            }
            Serial.println("OK");
        } else {
            if (bytes_read > 0) {
                Serial.println("ERROR: Unknown command");
            }
        }
    }
}

void loadConfig() {
    uint16_t magic;
    EEPROM.get(EEPROM_ADDR, magic);
    if (magic == EEPROM_MAGIC) {
        EEPROM.get(EEPROM_ADDR + sizeof(magic), keyMap);
    } else {
        memset(keyMap, 0, sizeof(keyMap)); 
        saveConfig();
    }
}

void saveConfig() {
    EEPROM.put(EEPROM_ADDR, EEPROM_MAGIC);
    EEPROM.put(EEPROM_ADDR + sizeof(uint16_t), keyMap);
}

int getFreeRam() {
    extern int __heap_start, *__brkval;
    int v;
    return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
