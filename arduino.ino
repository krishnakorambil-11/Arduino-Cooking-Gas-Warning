#include <Arduino.h>
#include <LiquidCrystal.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

// Timing Constants
constexpr unsigned long WARMUP_TIME_MS          = 20000UL;    // 20-second warmup phase
constexpr unsigned long REFRESH_INTERVAL_MS     = 1000UL;     // 1-second display update rate
constexpr unsigned long DRIFT_TIME_THRESHOLD_MS = 28800000UL; // 8 hours (8 * 60 * 60 * 1000)
constexpr unsigned long COMPLETE_MSG_HOLD_MS    = 1500UL;     // Non-blocking hold for "Warmup Complete"

// Sensor Calibration Constants
constexpr uint16_t DEFAULT_MIN_ADC            = 100;        // Baseline reading in clean air
constexpr uint16_t DEFAULT_MAX_ADC            = 800;        // Calibration max (blown-out candle)
constexpr uint8_t  ALERT_THRESHOLD_PCT        = 80;         // Warning threshold percentage
constexpr uint8_t  DRIFT_TOLERANCE            = 15;         // ± ADC reading deviation ("stable")

// Buzzer Timing Constants
constexpr unsigned long BUZZER_TOGGLE_INTERVAL_MS = 100UL;  // Time between HIGH/LOW toggles
constexpr uint8_t  BUZZER_BEEP_COUNT              = 3;      // Number of chirps per burst
constexpr uint8_t  BUZZER_TOGGLE_STEPS            = BUZZER_BEEP_COUNT * 2; // HIGH+LOW per beep

// Pin Definitions
constexpr uint8_t  LCD_RS                     = 12;         // LCD Register Select
constexpr uint8_t  LCD_EN                     = 11;         // LCD Enable
constexpr uint8_t  LCD_D4                     = 5;          // LCD Data 4
constexpr uint8_t  LCD_D5                     = 6;          // LCD Data 5
constexpr uint8_t  LCD_D6                     = 7;          // LCD Data 6
constexpr uint8_t  LCD_D7                     = 8;          // LCD Data 7
constexpr uint8_t  BUZZER_PIN                 = 9;          // Buzzer output
constexpr uint8_t  SENSOR_PIN                 = A0;         // MQ-2 analog input

// ============================================================================
// STATE VARIABLES
// ============================================================================

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Calibration & Baseline Tracking
uint16_t currentMinADC        = DEFAULT_MIN_ADC;
uint16_t currentMaxADC        = DEFAULT_MAX_ADC;

// Stability Detection
unsigned long stableStartTimestamp  = 0UL;
uint16_t lastStableADC              = 0;

// Timing & Display
unsigned long previousRefreshTimestamp  = 0UL;
unsigned long warmupStartTimestamp      = 0UL;
bool warmupComplete                     = false;

// Warmup Sub-State (for non-blocking "complete" message hold)
enum WarmupSubState : uint8_t { COUNTING, SHOWING_COMPLETE };
WarmupSubState warmupSubState  = COUNTING;
unsigned long completeMsgStart = 0UL;

// Non-blocking Buzzer State
bool buzzerActive               = false;
unsigned long lastBuzzerToggle  = 0UL;
uint8_t buzzerStep               = 0;

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
    Serial.begin(9600);

    // Initialize LCD
    lcd.begin(16, 2);
    lcd.clear();

    // Initialize Pins
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(SENSOR_PIN, INPUT);

    // Start warmup phase
    warmupStartTimestamp = millis();
    warmupComplete = false;
    warmupSubState = COUNTING;

    // Display initial warmup message
    lcd.setCursor(0, 0);
    lcd.print("MQ-2 Warming Up ");
    lcd.setCursor(0, 1);
    lcd.print("Time: 20s       ");
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Non-blocking buzzer pattern for alert conditions.
 * Re-armed externally (buzzerStep = 0) each time a new chirp burst should start.
 */
void handleBuzzerAlert(bool trigger) {
    if (!trigger) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerStep = 0;
        return;
    }

    unsigned long currentMs = millis();
    if (buzzerStep < BUZZER_TOGGLE_STEPS && (currentMs - lastBuzzerToggle >= BUZZER_TOGGLE_INTERVAL_MS)) {
        lastBuzzerToggle = currentMs;
        // Toggle buzzer state on even/odd steps
        digitalWrite(BUZZER_PIN, (buzzerStep % 2 == 0) ? HIGH : LOW);
        buzzerStep++;
    } else if (buzzerStep >= BUZZER_TOGGLE_STEPS) {
        digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer stays off between bursts
    }
}

/**
 * Map ADC reading to gas percentage using currentMin/Max calibration values.
 * 32-bit arithmetic prevents overflow when multiplying by 100.
 */
uint8_t calculateGasPercentage(uint16_t rawADC) {
    if (currentMaxADC <= currentMinADC) {
        return 0;
    }

    int32_t range = (int32_t)currentMaxADC - (int32_t)currentMinADC;
    int32_t offset = (int32_t)rawADC - (int32_t)currentMinADC;

    if (offset < 0) offset = 0;
    if (offset > range) offset = range;

    uint8_t percentage = (uint8_t)((offset * 100) / range);

    if (percentage > 100) percentage = 100;

    return percentage;
}

/**
 * Display gas level and status on 16x2 LCD.
 * Padded to prevent leftover ghost characters.
 */
void displayGasLevel(uint8_t gasPercentage, bool isWarning) {
    lcd.setCursor(0, 0);
    lcd.print("Gas Level: ");
    if (gasPercentage < 100) lcd.print(" ");
    if (gasPercentage < 10) lcd.print(" ");
    lcd.print(gasPercentage);
    lcd.print("% ");

    lcd.setCursor(0, 1);
    if (isWarning) {
        lcd.print("Status: WARNING!");
    } else {
        lcd.print("Status: Normal  ");
    }
}

/**
 * Handle baseline auto-recalibration logic.
 * Tracks stability and updates baseline if sensor remains stable for 8+ hours.
 */
void updateBaselineTracking(uint16_t rawADC) {
    int16_t drift = abs((int16_t)rawADC - (int16_t)lastStableADC);

    if (drift <= DRIFT_TOLERANCE) {
        unsigned long elapsedTime = millis() - stableStartTimestamp;

        if (elapsedTime >= DRIFT_TIME_THRESHOLD_MS) {
            currentMinADC = rawADC;
            lastStableADC = rawADC;
            stableStartTimestamp = millis();

            Serial.print("[BASELINE UPDATE] New minimum ADC: ");
            Serial.println(currentMinADC);
        }
    } else {
        lastStableADC = rawADC;
        stableStartTimestamp = millis();
    }
}

/**
 * Fully non-blocking warmup phase, including the post-countdown
 * "Warmup Complete" message hold (no delay() calls anywhere).
 * Returns true once warmup is fully finished.
 */
bool updateWarmupPhase() {
    unsigned long elapsedTime = millis() - warmupStartTimestamp;

    if (warmupSubState == SHOWING_COMPLETE) {
        if (millis() - completeMsgStart >= COMPLETE_MSG_HOLD_MS) {
            lcd.clear();

            lastStableADC = analogRead(SENSOR_PIN);
            stableStartTimestamp = millis();
            previousRefreshTimestamp = millis();

            return true;
        }
        return false;
    }

    if (elapsedTime >= WARMUP_TIME_MS) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Warmup Complete ");
        lcd.setCursor(0, 1);
        lcd.print("Initializing... ");

        completeMsgStart = millis();
        warmupSubState = SHOWING_COMPLETE;
        return false;
    }

    // Update countdown display every 500ms to reduce LCD flicker
    static unsigned long lastWarmupDisplay = 0UL;
    if (elapsedTime - lastWarmupDisplay >= 500UL) {
        lastWarmupDisplay = elapsedTime;

        uint8_t remainingSeconds = (WARMUP_TIME_MS - elapsedTime) / 1000UL;

        lcd.setCursor(0, 1);
        lcd.print("Time: ");
        if (remainingSeconds < 10) lcd.print(" ");
        lcd.print(remainingSeconds);
        lcd.print("s       ");
    }

    return false;
}

// ============================================================================
// MAIN LOOP (NON-BLOCKING)
// ============================================================================

void loop() {
    // WARMUP PHASE: fully non-blocking, including completion message
    if (!warmupComplete) {
        warmupComplete = updateWarmupPhase();
        return;
    }

    // Always service the buzzer pulse state machine every loop pass
    handleBuzzerAlert(buzzerActive);

    unsigned long currentTime = millis();
    if (currentTime - previousRefreshTimestamp >= REFRESH_INTERVAL_MS) {
        previousRefreshTimestamp = currentTime;

        // --- 1. Read Sensor ---
        uint16_t rawADC = analogRead(SENSOR_PIN);

        // --- 2. Calculate Gas Percentage ---
        uint8_t gasPercentage = calculateGasPercentage(rawADC);

        // --- 3. Update LCD Display ---
        bool isWarning = (gasPercentage >= ALERT_THRESHOLD_PCT);
        displayGasLevel(gasPercentage, isWarning);

        // --- 4. Buzzer Control ---
        // Re-arm the chirp burst every refresh cycle while warning persists,
        // so the alert repeats (not just once on entry) while gas stays high.
        if (isWarning) {
            buzzerActive = true;
            buzzerStep = 0;
        } else {
            buzzerActive = false;
        }

        // --- 5. Auto-Baseline Recalibration ---
        updateBaselineTracking(rawADC);

        // Serial logging for debugging
        Serial.print("ADC: ");
        Serial.print(rawADC);
        Serial.print(" | Gas%: ");
        Serial.print(gasPercentage);
        Serial.print(" | Status: ");
        Serial.println(isWarning ? "WARNING" : "NORMAL");
    }
}