/*
 * HUD Eyepiece — Multi-Mode Display with IMU Gesture Switching
 * XIAO MG24 Sense + Waveshare 1.51" Transparent OLED (SSD1309)
 *
 * Display modes cycle on a sharp head jerk (detected via gyroscope).
 * Current modes:
 *   0 — Starfield
 *   1 — FFT Spectrogram (frequency histogram)
 *   2 — IMU Horizon (pitch/roll artificial horizon)
 *   3 — Splash / info screen
 *
 * Wiring:
 *   VCC  -> 3V3      GND  -> GND
 *   DIN  -> D10       CLK  -> D8
 *   CS   -> D1        DC   -> D2        RST  -> D3
 *
 * Libraries:
 *   - U8g2            (olikraus)
 *   - arduinoFFT      (kosme)
 *   - Seeed_Arduino_LSM6DS3
 */

#include <U8g2lib.h>
#include <SPI.h>
#include <Wire.h>
#include <LSM6DS3.h>
#include <arduinoFFT.h>
#include <qrcode.h>

struct Star {
    float x, y, z;
};

// =============================================================
//  Hardware setup
// =============================================================

// Display — SSD1309 128x64 HW SPI
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(
    U8G2_MIRROR, /* cs=*/ D1, /* dc=*/ D2, /* rst=*/ D3
);

// IMU — LSM6DS3TR-C on I2C
LSM6DS3 imu(I2C_MODE, 0x6A);

// Microphone
#define MIC_PWR_PIN   PC8
#define MIC_ADC_PIN   PC9



// =============================================================
//  Mode management
// =============================================================

#define NUM_MODES  5
int currentMode = 0;

// Forward declarations for mode functions
void mode_starfield_init();
void mode_starfield_draw();
void mode_spectrogram_init();
void mode_spectrogram_draw();
void mode_horizon_init();
void mode_horizon_draw();
void mode_splash_init();
void mode_splash_draw();

typedef void (*ModeInitFn)();
typedef void (*ModeDrawFn)();

struct DisplayMode {
    const char* name;
    ModeInitFn  init;
    ModeDrawFn  draw;
};

void mode_clock_init();
void mode_clock_draw();

DisplayMode modes[NUM_MODES] = {
    { "STARFIELD",    mode_starfield_init,    mode_starfield_draw    },
    { "SPECTROGRAM",  mode_spectrogram_init,  mode_spectrogram_draw  },
    { "HORIZON",      mode_horizon_init,      mode_horizon_draw      },
    { "LINKEDIN",     mode_splash_init,       mode_splash_draw       },
    { "EARTH/MARS",   mode_clock_init,        mode_clock_draw        },
};



// =============================================================
//  MODE — Earth / Mars Clock (portrait orientation)
// =============================================================

// Set this to the current Unix timestamp (UTC) just before uploading.
// Get it from epochconverter.com
#define BOOT_UTC_EPOCH  1773541800UL

unsigned long bootMillis = 0;

void mode_clock_init() {
    bootMillis = millis();
}

void mode_clock_draw() {
    // Current UTC as Unix timestamp
    unsigned long elapsed = (millis() - bootMillis) / 1000UL;
    unsigned long utcNow = BOOT_UTC_EPOCH + elapsed;

    // --- Earth time (UTC) ---
    // unsigned long daySeconds = utcNow % 86400UL;
    // int eHour = daySeconds / 3600;
    // int eMin  = (daySeconds % 3600) / 60;
    // int eSec  = daySeconds % 60;

    // PDT = UTC - 7
    unsigned long daySeconds = utcNow % 86400UL;
    int localSeconds = (int)(daySeconds) - (7 * 3600);
    if (localSeconds < 0) localSeconds += 86400;
    int eHour = localSeconds / 3600;
    int eMin  = (localSeconds % 3600) / 60;
    int eSec  = localSeconds % 60;

    // --- Mars time (MTC) ---
    // Julian Date from Unix time
    double jd = ((double)utcNow / 86400.0) + 2440587.5;

    // Mars Sol Date
    double msd = (jd - 2405522.0028779) / 1.0274912517;

    // Mars Coordinated Time = fractional sol * 24 hours
    double mtcFrac = fmod(msd, 1.0);
    if (mtcFrac < 0.0) mtcFrac += 1.0;
    double mtcTotalSec = mtcFrac * 86400.0;  // Mars "seconds" in a sol
    int mHour = (int)(mtcTotalSec / 3600.0) % 24;
    int mMin  = (int)(fmod(mtcTotalSec, 3600.0) / 60.0);
    int mSec  = (int)(fmod(mtcTotalSec, 60.0));

    // Current sol number (for display)
    unsigned long solNumber = (unsigned long)msd;

    // --- Draw in portrait ---
    // Buffer X (0..127) = physical vertical, low X = bottom
    // Buffer Y (0..63) = physical horizontal
    // All text uses setFontDirection(1) for portrait reading

    u8g2.clearBuffer();
    u8g2.setFontDirection(1);

    char buf[24];

    // Earth section — upper portion of physical display (high buffer X)
    u8g2.setFont(u8g2_font_5x7_tr);
    const char* earthLabel = "EARTH PDT";
    int tw = u8g2.getStrWidth(earthLabel);
    u8g2.drawStr(90, (64 - tw) / 2, earthLabel);

    u8g2.setFont(u8g2_font_7x14B_tr);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", eHour, eMin, eSec);
    tw = u8g2.getStrWidth(buf);
    u8g2.drawStr(75, (64 - tw) / 2, buf);

    // Divider line across physical width at midpoint
    u8g2.setFontDirection(0);  // temporarily reset for line drawing
    u8g2.drawVLine(64, 0, 64);
    u8g2.setFontDirection(1);

    // Mars section — lower portion (low buffer X)
    u8g2.setFont(u8g2_font_5x7_tr);
    snprintf(buf, sizeof(buf), "SOL %lu", solNumber);
    tw = u8g2.getStrWidth(buf);
    u8g2.drawStr(52, (64 - tw) / 2, buf);

    u8g2.setFont(u8g2_font_7x14B_tr);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", mHour, mMin, mSec);
    tw = u8g2.getStrWidth(buf);
    u8g2.drawStr(37, (64 - tw) / 2, buf);

    // Small label at physical bottom
    u8g2.setFont(u8g2_font_4x6_tr);
    const char* mtcLabel = "MTC";
    tw = u8g2.getStrWidth(mtcLabel);
    u8g2.drawStr(28, (64 - tw) / 2, mtcLabel);

    u8g2.setFontDirection(0);  // reset for other modes
    u8g2.sendBuffer();
}

// =============================================================
//  Gesture detection (gyroscope-based)
// =============================================================

// We detect a sharp YAW (rotation around Z axis) as the switch gesture.
// This corresponds to a quick horizontal head turn.
// Threshold in degrees/second — tune to your preference.
#define GESTURE_GYRO_THRESHOLD  200.0f   // dps — how hard you need to jerk
#define GESTURE_COOLDOWN_MS     800      // ignore gestures for this long after one fires

unsigned long lastGestureTime = 0;

bool detectGesture() {
    float gz = imu.readFloatGyroX();

    // Check for sharp yaw in either direction
    if (fabs(gz) > GESTURE_GYRO_THRESHOLD) {
        unsigned long now = millis();
        if ((now - lastGestureTime) > GESTURE_COOLDOWN_MS) {
            lastGestureTime = now;
            return true;
        }
    }
    return false;
}

void switchMode() {
    currentMode = (currentMode + 1) % NUM_MODES;
    modes[currentMode].init();

    // Brief flash: show mode name for ~400ms
    u8g2.clearBuffer();
    /*
    u8g2.setFont(u8g2_font_7x14B_tr);
    int tw = u8g2.getStrWidth(modes[currentMode].name);
    u8g2.drawStr((128 - tw) / 2, 38, modes[currentMode].name);
    u8g2.sendBuffer();
    delay(400);
    */
    u8g2.sendBuffer();
}



// =============================================================
//  MODE 1 — FFT Spectrogram (frequency histogram)
// =============================================================

#define FFT_SAMPLES       256
#define FFT_SAMPLING_FREQ 8000
#define FFT_NUM_BARS      64
#define FFT_BAR_MAX_H      31
#define NOISE_FLOOR_DB     50.0
#define DYNAMIC_RANGE_DB   30.0
#define DECAY_FACTOR        0.25f


float vReal[FFT_SAMPLES];
float vImag[FFT_SAMPLES];
ArduinoFFT<float> FFT(vReal, vImag, FFT_SAMPLES, FFT_SAMPLING_FREQ);
float displayBars[FFT_NUM_BARS];


// State


// =============================================================
//  Whistle detector — simple 3-note sequence
// =============================================================

#define WHISTLE_MIN_MAG      8.0f
#define WHISTLE_SEMI_TOLERANCE  2    // +/- semitones for fuzz
#define WHISTLE_HOLD_MS      1000    // each note must sustain this long
#define WHISTLE_TIMEOUT_MS   4000    // reset if gap between notes too long
#define WHISTLE_MIN_FREQ   400.0f
#define WHISTLE_MAX_FREQ   2500.0f

const int targetNotes[] = { 90, 85, 88 };
#define TARGET_LEN  3

int   matchIndex = 0;              // which note we're waiting for next
unsigned long noteStartTime = 0;   // when current note started being heard
unsigned long lastHeardTime = 0;   // last frame we heard the target note
bool  trekTriggered = false;

int currentDetectedSemi = -1;

void resetTrekDetector() {
    matchIndex = 0;
    noteStartTime = 0;
    lastHeardTime = 0;
    trekTriggered = false;
    currentDetectedSemi = -1;
}

// Convert frequency to semitone number (A4=440Hz = semitone 69)
int freqToSemitone(float freq) {
    return (int)(12.0f * log2f(freq / 440.0f) + 69.0f + 0.5f);
}

// Find dominant tonal frequency from current FFT data
float findWhistlePitch() {
    int minBin = (int)(WHISTLE_MIN_FREQ / ((float)FFT_SAMPLING_FREQ / FFT_SAMPLES)) + 1;
    int maxBin = (int)(WHISTLE_MAX_FREQ / ((float)FFT_SAMPLING_FREQ / FFT_SAMPLES));
    if (maxBin >= FFT_SAMPLES / 2) maxBin = FFT_SAMPLES / 2 - 1;

    double peakMag = 0;
    int peakBin = 0;
    for (int i = minBin; i <= maxBin; i++) {
        if (vReal[i] > peakMag) {
            peakMag = vReal[i];
            peakBin = i;
        }
    }

    // Check magnitude is above noise
    double dB = (peakMag > 1.0) ? 20.0 * log10(peakMag) : 0.0;
    if (dB < WHISTLE_MIN_MAG + NOISE_FLOOR_DB) return 0.0f;

    // Check it's tonal — peak should dominate its neighbors
    double neighborAvg = 0;
    int count = 0;
    for (int i = peakBin - 3; i <= peakBin + 3; i++) {
        if (i >= minBin && i <= maxBin && i != peakBin) {
            neighborAvg += vReal[i];
            count++;
        }
    }
    if (count > 0) neighborAvg /= count;
    if (peakMag < neighborAvg * 3.0) return 0.0f;

    // Parabolic interpolation for sub-bin accuracy
    double alpha = (peakBin > 0) ? vReal[peakBin - 1] : 0;
    double beta  = vReal[peakBin];
    double gamma = (peakBin < FFT_SAMPLES / 2 - 1) ? vReal[peakBin + 1] : 0;
    double correction = 0.0;
    if (alpha + gamma > 0) {
        correction = 0.5 * (alpha - gamma) / (alpha - 2.0 * beta + gamma);
    }
    float refinedBin = (float)peakBin + (float)correction;

    return refinedBin * (float)FFT_SAMPLING_FREQ / FFT_SAMPLES;
}

bool checkForTrekWhistle() {
    if (trekTriggered) return false;

    unsigned long now = millis();

    // Timeout — reset if too long since we last heard the right note
    if (matchIndex > 0 && (now - lastHeardTime) > WHISTLE_TIMEOUT_MS) {
        Serial.println(">> Timeout, resetting.");
        matchIndex = 0;
    }

    float freq = findWhistlePitch();
    if (freq < 1.0f) return false;

    int semi = freqToSemitone(freq);
    int target = targetNotes[matchIndex];

    if (abs(semi - target) <= WHISTLE_SEMI_TOLERANCE) {
        // We're hearing the right note
        if (currentDetectedSemi != matchIndex) {
            // Just started hearing this target
            currentDetectedSemi = matchIndex;
            noteStartTime = now;
            Serial.print(">> Hearing note ");
            Serial.print(matchIndex);
            Serial.print(" (target=");
            Serial.print(target);
            Serial.print(" heard=");
            Serial.print(semi);
            Serial.println(")");
        }
        lastHeardTime = now;

        // Has it been held long enough?
        if ((now - noteStartTime) >= WHISTLE_HOLD_MS) {
            Serial.print(">> Note ");
            Serial.print(matchIndex);
            Serial.println(" CONFIRMED");
            matchIndex++;
            currentDetectedSemi = -1;

            if (matchIndex >= TARGET_LEN) {
                Serial.println("*** PATTERN MATCHED — ENGAGE! ***");
                trekTriggered = true;
                return true;
            }
        }
    } else {
        // Wrong note — but don't reset unless we've drifted far off
        // This allows sliding into a note
        if (currentDetectedSemi == matchIndex && abs(semi - target) > WHISTLE_SEMI_TOLERANCE + 2) {
            currentDetectedSemi = -1;
        }
    }

    return false;
}

// =============================================================
//  MODE 0 — Starfield with Warp Easter Egg
// =============================================================

#define NUM_STARS     64
#define MAX_DEPTH    256.0f
#define STAR_SPEED     4.0f

// Warp state
enum WarpState {
    WARP_IDLE,
    WARP_ACCELERATING,
    WARP_FLASH,
    WARP_COOLDOWN
};

WarpState warpState = WARP_IDLE;
unsigned long warpStartTime = 0;

#define WARP_ACCEL_DURATION_MS   2000   // ramp up over 2 seconds
#define WARP_FLASH_DURATION_MS    400   // white screen
#define WARP_COOLDOWN_DURATION_MS 500   // fade back to normal

Star stars[NUM_STARS];

// Store previous screen positions for trail drawing
int prevSX[NUM_STARS];
int prevSY[NUM_STARS];
bool hasPrev[NUM_STARS];

void initStar(Star &s, int idx) {
    s.x = (float)random(-128, 128);
    s.y = (float)random(-64, 64);
    s.z = (float)random(1, (int)MAX_DEPTH);
    hasPrev[idx] = false;
}

void mode_starfield_init() {
    for (int i = 0; i < NUM_STARS; i++) initStar(stars[i], i);
    warpState = WARP_IDLE;
    resetTrekDetector();
}

void triggerWarp() {
    warpState = WARP_ACCELERATING;
    warpStartTime = millis();
}

void mode_starfield_draw() {
    unsigned long now = millis();
    unsigned long elapsed = now - warpStartTime;

    // --- Warp state machine ---
    float speedMultiplier = 1.0f;
    float trailStrength = 0.0f;

    switch (warpState) {
        case WARP_IDLE:
            speedMultiplier = 1.0f;
            trailStrength = 0.0f;
            break;

        case WARP_ACCELERATING: {
            float t = (float)elapsed / WARP_ACCEL_DURATION_MS;
            if (t >= 1.0f) {
                warpState = WARP_FLASH;
                warpStartTime = now;
                break;
            }
            // Exponential ramp: 1x to ~20x
            speedMultiplier = 1.0f + 19.0f * t * t;
            // Trails fade in
            trailStrength = t;
            break;
        }

        case WARP_FLASH:
            if (elapsed > WARP_FLASH_DURATION_MS) {
                warpState = WARP_COOLDOWN;
                warpStartTime = now;
                // Reinit stars for clean return
                for (int i = 0; i < NUM_STARS; i++) initStar(stars[i], i);
            }
            // Full white screen
            u8g2.clearBuffer();
            for (int x = 0; x < 128; x++) {
                u8g2.drawVLine(x, 0, 64);
            }
            u8g2.sendBuffer();
            return;  // skip normal drawing

        case WARP_COOLDOWN:
            if (elapsed > WARP_COOLDOWN_DURATION_MS) {
                warpState = WARP_IDLE;
                resetTrekDetector();
            }
            // Fade from bright back to normal
            speedMultiplier = 1.0f;
            trailStrength = 0.0f;
            break;
    }

    // --- Run quick FFT for whistle detection (only when idle) ---
    static int fftSkipCounter = 0;
    if (warpState == WARP_IDLE && (++fftSkipCounter % 3 == 0)) {
        unsigned long periodUs = 1000000UL / FFT_SAMPLING_FREQ;
        double dcSum = 0.0;
        for (int i = 0; i < FFT_SAMPLES; i++) {
            unsigned long t0 = micros();
            vReal[i] = (double)analogRead(MIC_ADC_PIN);
            vImag[i] = 0.0;
            dcSum += vReal[i];
            while ((micros() - t0) < periodUs) {}
        }
        double dcOff = dcSum / FFT_SAMPLES;
        for (int i = 0; i < FFT_SAMPLES; i++) vReal[i] -= dcOff;

        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        if (checkForTrekWhistle()) {
            triggerWarp();
        }
    }

    // --- Draw starfield ---
    // During warp, don't fully clear — let old frames partially persist for trails
    if (trailStrength > 0.5f) {
        // Dim existing pixels by drawing semi-sparse black overlay
        // More trail = less clearing
        int clearDensity = (int)((1.0f - trailStrength) * 8) + 1;
        for (int x = 0; x < 128; x++) {
            for (int y = 0; y < 64; y++) {
                if ((x + y) % clearDensity == 0) {
                    u8g2.setDrawColor(0);
                    u8g2.drawPixel(x, y);
                    u8g2.setDrawColor(1);
                }
            }
        }
    } else {
        u8g2.clearBuffer();
    }

    float currentSpeed = STAR_SPEED * speedMultiplier;

    for (int i = 0; i < NUM_STARS; i++) {
        float oldZ = stars[i].z;

        stars[i].z -= currentSpeed;
        if (stars[i].z <= 0) {
            initStar(stars[i], i);
            stars[i].z = MAX_DEPTH;
            continue;
        }

        float invZ = 1.0f / stars[i].z;
        int sx = (int)(stars[i].x * (MAX_DEPTH * 0.5f) * invZ) + 64;
        int sy = (int)(stars[i].y * (MAX_DEPTH * 0.5f) * invZ) + 32;

        if (sx < 0 || sx >= 128 || sy < 0 || sy >= 64) {
            initStar(stars[i], i);
            continue;
        }

        float normZ = stars[i].z / MAX_DEPTH;

        if (trailStrength > 0.1f && hasPrev[i]) {
            // Draw streak from previous position to current
            u8g2.drawLine(prevSX[i], prevSY[i], sx, sy);
        } else {
            // Normal dot rendering
            if (normZ < 0.2f)       u8g2.drawBox(sx - 1, sy - 1, 3, 3);
            else if (normZ < 0.5f)  u8g2.drawBox(sx, sy, 2, 2);
            else                    u8g2.drawPixel(sx, sy);
        }

        prevSX[i] = sx;
        prevSY[i] = sy;
        hasPrev[i] = true;
    }

    u8g2.sendBuffer();
}

void mode_spectrogram_init() {
    memset(displayBars, 0, sizeof(displayBars));
}

void mode_spectrogram_draw() {
    // Sample audio
    unsigned long periodUs = 1000000UL / FFT_SAMPLING_FREQ;
    double dcSum = 0.0;
    for (int i = 0; i < FFT_SAMPLES; i++) {
        unsigned long t0 = micros();
        vReal[i] = (double)analogRead(MIC_ADC_PIN);
        vImag[i] = 0.0;
        dcSum += vReal[i];
        while ((micros() - t0) < periodUs) {}
    }

    // Remove DC offset
    double dcOff = dcSum / FFT_SAMPLES;
    for (int i = 0; i < FFT_SAMPLES; i++) vReal[i] -= dcOff;

    // FFT
    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    // Map to bars
    for (int i = 0; i < FFT_NUM_BARS; i++) {        
        double mag = vReal[i + 1];
        double dB = (mag > 1.0) ? 20.0 * log10(mag) : 0.0;
        dB -= NOISE_FLOOR_DB;
        if (dB < 0.0) dB = 0.0;
        float barH = (float)(dB / DYNAMIC_RANGE_DB) * FFT_BAR_MAX_H;
        if (barH > FFT_BAR_MAX_H) barH = FFT_BAR_MAX_H;

        if (barH >= displayBars[i]) displayBars[i] = barH;
        else displayBars[i] = displayBars[i] * DECAY_FACTOR + barH * (1.0f - DECAY_FACTOR);
    }

    // Draw
    u8g2.clearBuffer();
    for (int x = 0; x < FFT_NUM_BARS; x++) {
        int h = (int)(displayBars[x] + 0.5f);
        if (h > 0) {
            u8g2.drawVLine(63 + x, FFT_BAR_MAX_H - h + 1, h);
            u8g2.drawVLine(63 + x, FFT_BAR_MAX_H, h);
            u8g2.drawVLine(63 - x, FFT_BAR_MAX_H - h + 1, h);
            u8g2.drawVLine(63 - x, FFT_BAR_MAX_H, h);
        } 
        //u8g2.drawVLine(x, FFT_BAR_MAX_H - h + 1, h);
    }
    u8g2.drawHLine(0, FFT_BAR_MAX_H, 128);
    u8g2.sendBuffer();
}

// =============================================================
//  MODE 2 — Artificial Horizon (portrait, board rotated 90° CW)
// =============================================================

// When the board is mounted 90° clockwise (looking at the display):
//   Physical "up"      = board -X axis
//   Physical "right"   = board +Y axis
//   Physical "forward" = board +Z axis
//
// So we remap:
//   pitch = rotation around physical right axis = derived from board X & Z
//   roll  = rotation around physical forward axis = derived from board Y & Z

float smoothPitch = 0.0f;
float smoothRoll  = 0.0f;

float heading = 0.0f;           // relative heading in degrees, 0..360
unsigned long lastHeadingTime = 0;

#define HORIZON_ALPHA  0.15f

void mode_horizon_init() {
    smoothPitch = 0.0f;
    smoothRoll  = 0.0f;
    heading = 0.0f;
    lastHeadingTime = millis();
}

bool validate_coords(int x1, int x2, int y1, int y2) {
    if (x1 < 0) return false;
    if (x1 > 63) return false;
    if (x2 < 0) return false;
    if (x2 > 63) return false;
    if (y1 < 0) return false;
    if (y1 > 127) return false;
    if (y2 < 0) return false;
    if (y2 > 127) return false;
    return true;
}

void mode_horizon_draw() {
    float ax = imu.readFloatAccelX();
    float ay = imu.readFloatAccelY();
    float az = imu.readFloatAccelZ();

    // Pitch & roll
    float accelPitch = atan2(ax, sqrt(ay * ay + az * az)) * 57.2958f;
    float accelRoll  = atan2(az, -ay) * 57.2958f;

    smoothPitch = smoothPitch * (1.0f - HORIZON_ALPHA) + accelPitch * HORIZON_ALPHA;
    smoothRoll  = smoothRoll  * (1.0f - HORIZON_ALPHA) + accelRoll  * HORIZON_ALPHA;

    // Integrate gyro Y for heading (yaw)
    unsigned long now = millis();
    float dt = (now - lastHeadingTime) / 1000.0f;
    lastHeadingTime = now;
    float gyroY = imu.readFloatGyroY();
    heading += gyroY * dt;

    // Normalize to 0..360
    while (heading < 0.0f)   heading += 360.0f;
    while (heading >= 360.0f) heading -= 360.0f;

    // --- Draw ---
    u8g2.clearBuffer();

    // Portrait mapping:
    //   Buffer X (0..127) = physical vertical, top=0 bottom=127
    //   Buffer Y (0..63)  = physical horizontal, left=0 right=63

    int cx = 64;   // physical vertical center
    int cy = 32;   // physical horizontal center

    // --- Artificial horizon (upper portion) ---
    int pitchOffset = (int)(smoothPitch * 1.5f);
    float rollRad = smoothRoll * 0.01745f;
    float cosR = cos(rollRad);
    float sinR = sin(rollRad);

    int halfW = 40;
    int x0 = cx + pitchOffset - (int)(halfW * sinR);
    int y0 = cy - (int)(halfW * cosR);
    int x1 = cx + pitchOffset + (int)(halfW * sinR);
    int y1 = cy + (int)(halfW * cosR);
    // u8g2.drawLine(x0, y0, x1, y1);

    // Pitch ladder
    for (int deg = -30; deg <= 30; deg += 10) {
        if (deg == 0) continue;
        int ladderCX = cx + pitchOffset - (int)(deg * 1.5f);
        int ladderHalfW = 10;
        int lx0 = ladderCX - (int)(ladderHalfW * sinR);
        int ly0 = cy - (int)(ladderHalfW * cosR);
        int lx1 = ladderCX + (int)(ladderHalfW * sinR);
        int ly1 = cy + (int)(ladderHalfW * cosR);
        // if (validate_coords(lx0, ly0, lx1, ly1)) 
        u8g2.drawLine(lx0, ly0, lx1, ly1);
    }

    // Reticle
    u8g2.drawCircle(cx, cy, 4);
    u8g2.drawVLine(cx, cy - 10, 6);
    u8g2.drawVLine(cx, cy + 5, 6);
    u8g2.drawHLine(cx - 2, cy, 5);

// --- Compass ticker along physical bottom edge ---
    // Physical bottom = low buffer X. Ticks grow upward = increasing buffer X.

    int tickerBaseX = 2;             // buffer X baseline (near physical bottom)
    int tickerWidth = 64;            // spans full physical width (buffer Y)
    float degsPerPixel = 0.5f;
    float centerDeg = heading;

    // Separator line: runs across physical width at fixed height
    u8g2.drawVLine(tickerBaseX + 7, 0, tickerWidth);

    // Center marker: caret at physical center pointing downward toward ticks
    u8g2.drawPixel(tickerBaseX + 6, cy);
    u8g2.drawPixel(tickerBaseX + 5, cy - 1);
    u8g2.drawPixel(tickerBaseX + 5, cy + 1);

    for (int py = 0; py < tickerWidth; py++) {
        float deg = centerDeg + (py - tickerWidth / 2) * degsPerPixel;

        while (deg < 0.0f)   deg += 360.0f;
        while (deg >= 360.0f) deg -= 360.0f;

        int ideg = ((int)(deg + 0.5f)) % 360;

        if (ideg % 45 == 0) {
            // Major tick: 5px upward from baseline
            u8g2.drawHLine(tickerBaseX, py, 5);

            const char* label = "";
            switch (ideg) {
                case 0:   label = "N"; break;
                case 45:  label = "NE"; break;
                case 90:  label = "E"; break;
                case 135: label = "SE"; break;
                case 180: label = "S"; break;
                case 225: label = "SW"; break;
                case 270: label = "W"; break;
                case 315: label = "NW"; break;
            }
            u8g2.setFontDirection(1);
            u8g2.setFont(u8g2_font_4x6_tr);
            u8g2.drawStr(tickerBaseX - 6, py - 1, label);
            u8g2.setFontDirection(0);
        } else if (ideg % 15 == 0) {
            u8g2.drawHLine(tickerBaseX, py, 3);
        } else if (ideg % 5 == 0) {
            u8g2.drawHLine(tickerBaseX, py, 1);
        }
    }

    // Heading readout next to pitch/roll
    // snprintf(buf, sizeof(buf), "H%.0f", heading);
    // u8g2.setFont(u8g2_font_5x7_tr);
    // u8g2.drawStr(2, 28, buf);

    u8g2.sendBuffer();
}

// =============================================================
//  MODE 3 — QR Code (LinkedIn) — Portrait layout
// =============================================================

// Buffer is 128x64 but physically mounted as 64 wide x 128 tall.
// Buffer X (0..127) = physical vertical (top=0, bottom=127)
// Buffer Y (0..63)  = physical horizontal (left=0, right=63)

#include <qrcode.h>

#define QR_VERSION      4     // 33x33 modules
#define QR_MODULE_SIZE  1     // 1px per module

QRCode qrcode;
uint8_t qrcodeData[256];
bool qrGenerated = false;

void mode_splash_init() {
    if (!qrGenerated) {
        qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW,
                        "https://www.linux.org");
        qrGenerated = true;
    }
}

void mode_splash_draw() {
    u8g2.clearBuffer();

    int qrSize = qrcode.size;  // 33

    // Center QR code in buffer Y (physical horizontal), upper area in buffer X
    int offsetY = (64 - qrSize * QR_MODULE_SIZE) / 2;
    int offsetX = 20;

    // Draw QR code (pixels have no orientation, this stays the same)
    for (int qy = 0; qy < qrSize; qy++) {
        for (int qx = 0; qx < qrSize; qx++) {
            if (qrcode_getModule(&qrcode, qx, qy)) {
                u8g2.drawPixel(offsetX + qy * QR_MODULE_SIZE,
                               offsetY + qx * QR_MODULE_SIZE);
            }
        }
    }

    // Portrait text: direction 1 flows text along buffer +Y (physical left-to-right)
    // Character tops point toward buffer -X (physical up)
    u8g2.setFontDirection(1);

    int lineX = offsetX + qrSize + 10;  // first line below QR in physical space

    u8g2.setFont(u8g2_font_7x14B_tr);

    const char* name1 = "Lastname";
    int tw1 = u8g2.getStrWidth(name1);
    u8g2.drawStr(lineX, (64 - tw1) / 2, name1);

    lineX += 16;
    const char* name2 = "Firstname";
    int tw2 = u8g2.getStrWidth(name2);
    u8g2.drawStr(lineX, (64 - tw2) / 2, name2);

    // Reset direction so other modes aren't affected
    u8g2.setFontDirection(0);

    u8g2.sendBuffer();
}

// =============================================================
//  Setup & Loop
// =============================================================

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(D0));

    // Power on microphone
    pinMode(MIC_PWR_PIN, OUTPUT);
    digitalWrite(MIC_PWR_PIN, HIGH);

    // Power on IMU
    pinMode(PD5, OUTPUT);
    digitalWrite(PD5, HIGH);
    delay(200);

    // Init IMU
    if (imu.begin() != 0) {
        Serial.println("IMU init failed!");
    } else {
        Serial.println("IMU ready.");
    }

    // Init display
    u8g2.begin();
    u8g2.setContrast(200);

    // Init first mode
    modes[currentMode].init();

    Serial.println("HUD ready. Jerk head to switch modes.");
}

void loop() {
    // Check for gesture (runs every frame, very fast)
    if (detectGesture()) {
        Serial.print("Gesture! Switching to mode ");
        switchMode();
        Serial.println(modes[currentMode].name);
    }

    // Draw current mode
    modes[currentMode].draw();
}
