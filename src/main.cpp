#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ─── Pines ─────────────────────────────────────────────────
#define BEEPER          27
#define POT_A           14     // Potenciómetro jugador A
#define POT_B           12     // Potenciómetro jugador B
#define PIN_NEOPIXEL    13     // Pin neopixel

// ─── Configuración pantalla/LED ───────────────────────────────
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR       0x3C
#define NUM_LEDS        16

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel ring(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);


// ─── Constantes del juego ──────────────────────────────────
#define FONT_SIZE       2
#define PADDLE_W        4
#define PADDLE_H       10
#define PADDLE_PAD     10
#define BALL_SIZE       3
#define SCORE_PAD      10

#define EFFECT_SPEED   0.5f
#define MIN_Y_SPEED    0.5f
#define MAX_Y_SPEED    2.0f

// ─── Variables globales (compartidas entre tareas) ─────────
volatile float ballX      = SCREEN_WIDTH / 2.0f;
volatile float ballY      = SCREEN_HEIGHT / 2.0f;
volatile float ballSpeedX = 1.0f;
volatile float ballSpeedY = 1.0f;

volatile int paddleA = 30;
volatile int paddleB = 30;

volatile int scoreA = 0;
volatile int scoreB = 0;

volatile bool gameRunning = false;

// Semáforo / mutex para proteger variables compartidas
SemaphoreHandle_t gameMutex = NULL;

// Estados de sonido (simples, no bloqueantes)
volatile bool playBounce = false;
volatile bool playPoint  = false;

// ─── Prototipos de tareas ──────────────────────────────────
void TaskReadControls(void *pvParameters);
void TaskUpdatePhysics(void *pvParameters);
void TaskRenderDisplay(void *pvParameters);
void TaskSound(void *pvParameters);

// ─── Funciones auxiliares ──────────────────────────────────
void taskNeoPixel(void *pvParameters) {
  ring.begin();
  //ring.setBrightness(60); // Moderate brightness for battery longevity
  while (true) {
    // Show RED
    for(int i=0; i<NUM_LEDS; i++) ring.setPixelColor(i, ring.Color(255, 0, 0));
    ring.show();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Show GREEN
    for(int i=0; i<NUM_LEDS; i++) ring.setPixelColor(i, ring.Color(0, 255, 0));
    ring.show();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Show BLUE
    for(int i=0; i<NUM_LEDS; i++) ring.setPixelColor(i, ring.Color(0, 0, 255));
    ring.show();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void centerPrint(const char* text, int y, int sz) {
  display.setTextSize(sz);
  int w = strlen(text) * 6 * sz;
  display.setCursor(SCREEN_WIDTH / 2 - w / 2, y);
  display.print(text);
}
void showSplashScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  centerPrint("PONG", 8, 3);
  centerPrint("Move paddles", 30, 1);
  centerPrint("to start", 42, 1);
  display.display();
}

void drawSplash() {
  // Puedes poner animación simple aquí si quieres
  showSplashScreen();  // por simplicidad
}

void resetBall() {
  ballX = SCREEN_WIDTH / 2.0f;
  ballY = random(10, SCREEN_HEIGHT - 20);
  ballSpeedX = (random(-1, 1) == 0) ? 2.2f : -2.2f;
  ballSpeedY =  random(-12, 13) / 10.0f;  // -1.2 a +1.2
}

void addSpin(int paddleSpeed) {
  ballSpeedY += paddleSpeed * EFFECT_SPEED * 0.7f;

  if (abs(ballSpeedY) < MIN_Y_SPEED)
    ballSpeedY = (ballSpeedY >= 0) ? MIN_Y_SPEED : -MIN_Y_SPEED;

  if (abs(ballSpeedY) > MAX_Y_SPEED)
    ballSpeedY = (ballSpeedY > 0) ? MAX_Y_SPEED : -MAX_Y_SPEED;
}

void playStartSound() {
  // Secuencia corta no bloqueante (se maneja en TaskSound si quieres)
  tone(BEEPER, 400, 80);
  vTaskDelay(pdMS_TO_TICKS(100));
  tone(BEEPER, 700, 80);
}


// ─── SETUP ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(BEEPER, OUTPUT);
  noTone(BEEPER);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 init failed"));
    for(;;);
  }

  display.clearDisplay();
  display.display();
  display.setTextWrap(false);

  gameMutex = xSemaphoreCreateMutex();
  if (gameMutex == NULL) {
    Serial.println("No se pudo crear mutex!");
    for(;;);
  }

  // Crear tareas
  xTaskCreate(taskNeoPixel, "LED_Task", 3000, NULL, 1, NULL);
  xTaskCreate(
    TaskReadControls,      // función
    "ReadControls",        // nombre (debug)
    2048,                  // stack size (bytes) — importante en ESP32
    NULL,
    2,                     // prioridad (1=baja, configMAX_PRIORITIES-1=alta)
    NULL
  );

  xTaskCreate(
    TaskUpdatePhysics,
    "Physics",
    2048,
    NULL,
    3,                     // física más prioritaria que render
    NULL
  );

  xTaskCreate(
    TaskRenderDisplay,
    "Display",
    3072,                  // más stack por uso de Adafruit_GFX
    NULL,
    1,
    NULL
  );

  xTaskCreate(
    TaskSound,
    "Sound",
    1024,
    NULL,
    2,
    NULL
  );

  showSplashScreen();

  // El scheduler de FreeRTOS ya está corriendo automáticamente en ESP32 Arduino
  // NO se necesita vTaskStartScheduler() manualmente
}

// ─── LOOP vacío ────────────────────────────────────────────
void loop() {
  // En FreeRTOS → el loop queda vacío
  vTaskDelay(portMAX_DELAY);
}

// ─── Tarea 1: Leer potenciómetros ──────────────────────────
void TaskReadControls(void *pvParameters) {
  for (;;) {
    int valA = analogRead(POT_A);
    int valB = analogRead(POT_B);

    if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      paddleA = map(valA, 0, 4095, 0, SCREEN_HEIGHT - PADDLE_H);
      paddleB = map(valB, 0, 4095, 0, SCREEN_HEIGHT - PADDLE_H);
      xSemaphoreGive(gameMutex);
    }

    // Si estamos en splash y hay movimiento → iniciar juego
    if (!gameRunning) {
      static int prevA = analogRead(POT_A), prevB = analogRead(POT_B);
      if ((valA!=prevA) || (valB!=prevB)) {
        if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          resetBall();
          scoreA = scoreB = 0;
          gameRunning = true;
          xSemaphoreGive(gameMutex);
        }
        playStartSound();  // no bloqueante
      }
      prevA = valA;
      prevB = valB;
    }

    vTaskDelay(pdMS_TO_TICKS(20));  // ~50 Hz lectura
  }
}

// ─── Tarea 2: Física del juego ─────────────────────────────
void TaskUpdatePhysics(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();

  for (;;) {
    if (!gameRunning) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      // Guardar velocidades de pala para efecto
      static int lastPA = 0, lastPB = 0;
      int speedA = paddleA - lastPA;
      int speedB = paddleB - lastPB;
      lastPA = paddleA;
      lastPB = paddleB;

      // Mover pelota
      ballX += ballSpeedX;
      ballY += ballSpeedY;

      // Rebotes superior/inferior
      if (ballY <= 0 || ballY >= SCREEN_HEIGHT - BALL_SIZE) {
        ballSpeedY = -ballSpeedY;
        playBounce = true;
      }

      // Pala izquierda (A)
      if (ballX <= PADDLE_PAD + PADDLE_W &&
          ballX >= PADDLE_PAD - BALL_SIZE &&
          ballSpeedX < 0) {
        if (ballY + BALL_SIZE > paddleA && ballY < paddleA + PADDLE_H) {
          ballSpeedX = -ballSpeedX;
          addSpin(speedA);
          playBounce = true;
        }
      }

      // Pala derecha (B)
      if (ballX >= SCREEN_WIDTH - PADDLE_PAD - PADDLE_W - BALL_SIZE &&
          ballX <= SCREEN_WIDTH - PADDLE_PAD &&
          ballSpeedX > 0) {
        if (ballY + BALL_SIZE > paddleB && ballY < paddleB + PADDLE_H) {
          ballSpeedX = -ballSpeedX;
          addSpin(speedB);
          playBounce = true;
        }
      }

      // Punto / gol
      if (ballX < -10 || ballX > SCREEN_WIDTH + 10) {
        if (ballSpeedX > 0) scoreA++; else scoreB++;
        playPoint = true;
        resetBall();
      }

      xSemaphoreGive(gameMutex);
    }

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000 / 60));  // ~60 Hz física
  }
}

// ─── Tarea 3: Dibujar en pantalla ──────────────────────────
void TaskRenderDisplay(void *pvParameters) {
  for (;;) {
    display.clearDisplay();

    if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(8)) == pdTRUE) {
      if (!gameRunning) {
        drawSplash();
      } else {
        // Palas
        display.fillRect(PADDLE_PAD, paddleA, PADDLE_W, PADDLE_H, WHITE);
        display.fillRect(SCREEN_WIDTH - PADDLE_PAD - PADDLE_W, paddleB, PADDLE_W, PADDLE_H, WHITE);

        // Línea central
        for (int i = 0; i < SCREEN_HEIGHT; i += 6)
          display.drawFastVLine(SCREEN_WIDTH / 2, i, 4, WHITE);

        // Pelota
        display.fillRect((int)ballX, (int)ballY, BALL_SIZE, BALL_SIZE, WHITE);

        // Marcador
        display.setTextSize(FONT_SIZE);
        display.setTextColor(WHITE);
        String sa = String(scoreA);
        String sb = String(scoreB);
        int wa = sa.length() * 6 * FONT_SIZE;
        display.setCursor(SCREEN_WIDTH/2 - SCORE_PAD - wa, 4);
        display.print(sa);
        display.setCursor(SCREEN_WIDTH/2 + SCORE_PAD + 2, 4);
        display.print(sb);
      }

      xSemaphoreGive(gameMutex);
    }

    display.display();

    vTaskDelay(pdMS_TO_TICKS(1000 / 45));  // ~45 fps OLED
  }
}

// ─── Tarea 4: Sonido (no bloqueante) ───────────────────────
void TaskSound(void *pvParameters) {
  for (;;) {
    if (playBounce) {
      tone(BEEPER, 600, 40);
      playBounce = false;
    }
    if (playPoint) {
      tone(BEEPER, 180, 180);
      playPoint = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // chequeo rápido
  }
}

