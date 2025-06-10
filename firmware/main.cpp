#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HID-Project.h>
#include <EEPROM.h>
#include <string.h>

// --- 定数定義 ---
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);
const uint8_t ENCODER_A_PIN = 0, ENCODER_B_PIN = 1, ENCODER_SW_PIN = A0;
const uint8_t KEY_PINS[] = {4, 5, 6, 7, 8, 9, 10, 16};
const uint8_t NUM_KEYS = sizeof(KEY_PINS) / sizeof(KEY_PINS[0]);
const unsigned long DEBOUNCE_DELAY = 5;
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
bool currentKeyStates[NUM_KEYS] = {false}, lastKeyStates[NUM_KEYS] = {false};
unsigned long lastDebounceTimes[NUM_KEYS] = {0};
bool currentEncoderSwState = false, lastEncoderSwState = false;
unsigned long lastEncoderSwDebounceTime = 0;
volatile long encoderCount = 0;
volatile uint8_t encoderState = 0;
long lastProcessedEncoderCount = 0;
long encoderCountAtSwPress = 0;

// --- UIおよび曲名スクロール関連 ---
constexpr size_t MAX_SONG_LEN = 64;
char     currentSongName[MAX_SONG_LEN] = "No Song Info";
bool     isPlaying = false;
const int DISPLAY_WIDTH = 128;
const int SONG_NAME_Y = 16;
int      song_name_pixel_width = 0;
int      scroll_offset_x = 0;
unsigned long last_scroll_time = 0;
// ★★★ 更新間隔を安定動作する値に戻す ★★★
const unsigned long SCROLL_INTERVAL = 30;

// --- 関数のプロトタイプ宣言 ---
void loadConfig();
void saveConfig();
void handleEncoder();
void setup();
void loop();
void processEncoder();
void scanEncoderSwitch();
void scanKeys();
void handleSerialCommands();
void executeMapping(int mapIndex, bool pressed);
void drawUI(bool playing, const char* song_name);

// --- 割り込みサービスルーチン ---
void handleEncoder() {
    const int8_t lookup_table[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    uint8_t currentState = (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    uint8_t index = (encoderState << 2) | currentState;
    int8_t direction = lookup_table[index];
    if (direction != 0) { encoderCount -= direction; }
    encoderState = currentState;
}

// --- セットアップ ---
void setup() {
    Serial.begin(115200);
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
    attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), handleEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), handleEncoder, CHANGE);

    u8g2.setFont(u8g2_font_6x10_tf);
    song_name_pixel_width = u8g2.getStrWidth(currentSongName);
    drawUI(isPlaying, currentSongName);
}

// --- メインループ ---
void loop() {
    handleSerialCommands();
    scanEncoderSwitch();
    processEncoder();
    scanKeys();

    if (song_name_pixel_width > DISPLAY_WIDTH) {
        unsigned long current_time = millis();
        if (current_time - last_scroll_time > SCROLL_INTERVAL) {
            last_scroll_time = current_time;
            
            // ★★★ 1回の描画で2ピクセル動かし、処理負荷を軽減 ★★★
            scroll_offset_x -= 2;

            constexpr int gap = 40;
            if (scroll_offset_x <= (-song_name_pixel_width - gap)) {
                // ループの継ぎ目でズレが生じないよう、差分を考慮してオフセットを再計算
                scroll_offset_x += (song_name_pixel_width + gap);
            }
            
            drawUI(isPlaying, currentSongName);
        }
    }
}

void drawUI(bool playing, const char* song_name) {
    constexpr int W = 128;
    constexpr int H = 64;
    constexpr int iconW = 16;
    constexpr int spacing = 24;

    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_6x10_tf);
        
        if (song_name_pixel_width > W) {
            constexpr int gap = 40;
            u8g2.drawStr(scroll_offset_x, SONG_NAME_Y, song_name);
            u8g2.drawStr(scroll_offset_x + song_name_pixel_width + gap, SONG_NAME_Y, song_name);
        } else {
            int16_t tw = song_name_pixel_width;
            u8g2.drawStr((W - tw) / 2, SONG_NAME_Y, song_name);
        }

        const int cy      = 48;
        const int cx_play = W / 2;
        const int cx_prev = cx_play - (iconW + spacing);
        const int cx_next = cx_play + (iconW + spacing);

        // (アイコン描画部分は変更なし)
        {
            int barW = 2, barH = 14;
            u8g2.drawBox(cx_prev + iconW / 2 - barW, cy - barH / 2, barW, barH);
            u8g2.drawTriangle(cx_prev - iconW / 2 + 2, cy, cx_prev + iconW / 2 - barW - 2, cy - barH / 2, cx_prev + iconW / 2 - barW - 2, cy + barH / 2);
            u8g2.drawTriangle(cx_prev - iconW / 2 + 6, cy, cx_prev + iconW / 2 - barW - 6, cy - (barH / 2 - 2), cx_prev + iconW / 2 - barW - 6, cy + (barH / 2 - 2));
        }
        {
            int barW = 3, barH = 16;
            if (playing) {
                u8g2.drawBox(cx_play - barW - 1, cy - barH / 2, barW, barH);
                u8g2.drawBox(cx_play + 1, cy - barH / 2, barW, barH);
            } else {
                u8g2.drawTriangle(cx_play - iconW / 2 + 1, cy - barH / 2, cx_play - iconW / 2 + 1, cy + barH / 2, cx_play + iconW / 2 - 1, cy);
            }
        }
        {
            int barW = 2, barH = 14;
            u8g2.drawTriangle(cx_next + iconW / 2 - 6, cy, cx_next - iconW / 2 + 6, cy - (barH / 2 - 2), cx_next - iconW / 2 + 6, cy + (barH / 2 - 2));
            u8g2.drawTriangle(cx_next + iconW / 2 - 2, cy, cx_next - iconW / 2 + barW + 2, cy - barH / 2, cx_next - iconW / 2 + barW + 2, cy + barH / 2);
            u8g2.drawBox(cx_next - iconW / 2, cy - barH / 2, barW, barH);
        }
    } while (u8g2.nextPage());
}

// --- 機能別関数 (以下、変更なし) ---
void processEncoder() {
    noInterrupts(); long c = encoderCount; interrupts();
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
    
    if (mapIndex == ENCODER_CW_INDEX || mapIndex == ENCODER_CCW_INDEX || 
        mapIndex == ENCODER_SW_CW_INDEX || mapIndex == ENCODER_SW_CCW_INDEX) {
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

void scanEncoderSwitch() {
    unsigned long currentTime = millis();
    bool reading = (digitalRead(ENCODER_SW_PIN) == LOW);

    if (reading != lastEncoderSwState) {
        lastEncoderSwDebounceTime = currentTime;
    }

    if ((currentTime - lastEncoderSwDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != currentEncoderSwState) {
            bool newState = reading;
            if (newState) {
                noInterrupts();
                encoderCountAtSwPress = encoderCount;
                interrupts();
            } else {
                noInterrupts();
                long cntNow = encoderCount;
                interrupts();
                if (cntNow == encoderCountAtSwPress) {
                    executeMapping(ENCODER_SW_INDEX, true);
                    executeMapping(ENCODER_SW_INDEX, false);
                }
            }
            currentEncoderSwState = newState;
        }
    }
    lastEncoderSwState = reading;
}

void scanKeys() {
    unsigned long currentTime = millis();
    for (int i = 0; i < NUM_KEYS; i++) {
        bool reading = (digitalRead(KEY_PINS[i]) == LOW);
        if (reading != lastKeyStates[i]) {
            lastDebounceTimes[i] = currentTime;
        }
        if ((currentTime - lastDebounceTimes[i]) > DEBOUNCE_DELAY) {
            if (reading != currentKeyStates[i]) {
                currentKeyStates[i] = reading;
                executeMapping(i, currentKeyStates[i]);
            }
        }
        lastKeyStates[i] = reading;
    }
}


void handleSerialCommands() {
    if (Serial.available() > 0) {
        char line_buffer[256]; 
        int bytes_read = Serial.readBytesUntil('\n', line_buffer, sizeof(line_buffer) - 1);
        line_buffer[bytes_read] = '\0';

        char* command = line_buffer;
        while (*command == ' ' || *command == '\t' || *command == '\r') {
            command++;
        }

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
        } else if (strncmp(command, "SONG_INFO:", 10) == 0) {
            char* data = command + 10;
            char* p = strtok(data, ",");
            if (p != nullptr) {
                strncpy(currentSongName, p, MAX_SONG_LEN - 1);
                currentSongName[MAX_SONG_LEN - 1] = '\0';
                p = strtok(nullptr, ",");
                if (p != nullptr) { isPlaying = (atoi(p) != 0); }
                
                u8g2.setFont(u8g2_font_6x10_tf);
                song_name_pixel_width = u8g2.getStrWidth(currentSongName);
                
                if (song_name_pixel_width > DISPLAY_WIDTH) {
                    scroll_offset_x = 0;
                }
                
                last_scroll_time = millis();
                drawUI(isPlaying, currentSongName);
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