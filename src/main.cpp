#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ─── Pines ─────────────────────────────────────────────────
#define BEEPER          27
#define POT_A           35     // Potenciómetro jugador A
#define POT_B           34     // Potenciómetro jugador B
#define PIN_NEOPIXEL    13     // Pin neopixel

// ─── Configuración pantalla/LED ───────────────────────────────
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR       0x3C
#define NUM_LEDS        16


#define NOTE_D4  294
#define NOTE_D5  587
#define NOTE_A4  440
#define NOTE_GS4 415
#define NOTE_G4  392
#define NOTE_F4  349
#define NOTE_C4  262
#define NOTE_B3  247

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
#define MIN_Y_SPEED    0.0f
#define MAX_Y_SPEED    2.0f

enum GameState{
  SPLASH,
  STARTING,
  PLAYING,
  GAME_OVER
};

// ─── Variables globales (compartidas entre tareas) ─────────
volatile float ballX      = SCREEN_WIDTH / 2.0f;
volatile float ballY      = SCREEN_HEIGHT / 2.0f;
volatile float ballSpeedX = 0.0f;
volatile float ballSpeedY = 0.0f;

volatile int paddleA = 30;
volatile int paddleB = 30;

volatile int scoreA = 0;
volatile int scoreB = 0;

volatile int menuOption = 0; // 0 = STAY, 1 = PLAY

volatile bool roundJustStarted = false;
volatile bool gameRunning = false;

int songIndex = 0;
TickType_t nextNoteTime = 0;

volatile GameState gameState= SPLASH; 

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


void playSongStep() {
  const int melody[] = {
    NOTE_D4, NOTE_D4, NOTE_D5, NOTE_A4,
    NOTE_GS4, NOTE_G4, NOTE_F4,
    NOTE_D4, NOTE_F4, NOTE_G4
  };

  const int durations[] = {
    60, 60, 120, 180,
    120, 120, 120,
    60, 60, 60
  };

  const int length = sizeof(melody) / sizeof(melody[0]);

  TickType_t now = xTaskGetTickCount();

  if (now >= nextNoteTime) {
    tone(BEEPER, melody[songIndex], durations[songIndex]);

    nextNoteTime = now + pdMS_TO_TICKS(durations[songIndex] + 20);

    songIndex++;
    if (songIndex >= length) {
      songIndex = 0; // loop song
    }
  }
}
void centerPrint(const char* text, int y, int sz) {
  display.setTextSize(sz);
  int w = strlen(text) * 6 * sz;
  display.setCursor(SCREEN_WIDTH / 2 - w / 2, y);
  display.print(text);
}
void updateScore(){
  ring.clear();
  for(int i=0; i<8 && i<scoreA; i++) ring.setPixelColor(i, ring.Color(255, 0, 0));
  ring.show();
  vTaskDelay(pdMS_TO_TICKS(10));
  
  for(int i=8; i<16 && i<scoreB+8; i++) ring.setPixelColor(i, ring.Color(255, 255, 255));
  ring.show();
  vTaskDelay(pdMS_TO_TICKS(10));
}
void ringSplash(){
  ring.clear();
  for(int i=0; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 0, 0));
  for(int i=1; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 255, 255));
  ring.show();
  vTaskDelay(pdMS_TO_TICKS(10));
}
void showSplashScreen() {
  /*ring.clear();
  for(int i=0; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 0, 0));
  for(int i=1; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 255, 255));
  ring.show();*/
  vTaskDelay(pdMS_TO_TICKS(10));
  display.clearDisplay();
  display.setTextColor(WHITE);
  centerPrint("KITTYPONG", 8, 2);
  centerPrint("Move paddles", 30, 1);
  centerPrint("to start", 42, 1);
  display.display();
}
void playStartSound() {
  // Secuencia corta no bloqueante (se maneja en TaskSound si quieres)
  tone(BEEPER, 400, 80);
  vTaskDelay(pdMS_TO_TICKS(100));
  tone(BEEPER, 700, 80);
}

void drawSplash(int selectedOption) {
  /*ring.clear();
  for(int i=0; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 0, 0));
  for(int i=1; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 255, 255));
  ring.show();*/
  playSongStep();
  vTaskDelay(pdMS_TO_TICKS(10));
  display.clearDisplay();
  display.setTextColor(WHITE);
  centerPrint("KITTYPONG", 4, 2);

  // STAY option
  if (selectedOption == 0) display.fillRect(14, 28, 100, 18, WHITE);
  display.setTextColor(selectedOption == 0 ? BLACK : WHITE);
  centerPrint("STAY", 32, 1);

  // PLAY option
  if (selectedOption == 1) display.fillRect(14, 46, 100, 18, WHITE);
  display.setTextColor(selectedOption == 1 ? BLACK : WHITE);
  centerPrint("PLAY", 50, 1);
}

void resetBall() {
  ballX = SCREEN_WIDTH / 2.0f;
  ballY = random(10, SCREEN_HEIGHT - 20);
  ballSpeedX = (random(-1, 1) == 0) ? 1.0f : -1.0f;
  ballSpeedY =  random(-12, 13) / 10.0f;  // -1.2 a +1.2
}

void addSpin(int paddleSpeed) {
  ballSpeedY += paddleSpeed * EFFECT_SPEED * 0.7f;

  if (abs(ballSpeedY) < MIN_Y_SPEED)
    ballSpeedY = (ballSpeedY >= 0) ? MIN_Y_SPEED : -MIN_Y_SPEED;

  if (abs(ballSpeedY) > MAX_Y_SPEED)
    ballSpeedY = (ballSpeedY > 0) ? MAX_Y_SPEED : -MAX_Y_SPEED;
}


void resetGame() {
  scoreA = 0;
  scoreB = 0;

  paddleA = 30;
  paddleB = 30;

  resetBall();

  ballSpeedX = (random(0,2)==0) ? 2.2f : -2.2f;
  ballSpeedY = 1.0f;
  roundJustStarted = true;
}
// ─── SETUP ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(BEEPER, OUTPUT);
  noTone(BEEPER);
  ring.begin();

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
  //xTaskCreate(taskNeoPixel, "LED_Task", 3000, NULL, 1, NULL);
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
  float smoothA = analogRead(POT_A);
  float smoothB = analogRead(POT_B);
  const float alpha = 0.2f;

  int selectedOption = 0;       // 0 = STAY, 1 = PLAY
  TickType_t playHeldSince = 0; // when PLAY was first selected
  bool playTimerActive = false;

  for (;;) {
    int rawA = analogRead(POT_A);
    int rawB = analogRead(POT_B);

    smoothA = alpha * rawA + (1 - alpha) * smoothA;
    smoothB = alpha * rawB + (1 - alpha) * smoothB;

    if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      paddleA = map((int)smoothA, 0, 4095, 0, SCREEN_HEIGHT - PADDLE_H);
      paddleB = map((int)smoothB, 0, 4095, 0, SCREEN_HEIGHT - PADDLE_H);
      xSemaphoreGive(gameMutex);
    }

    if (gameState == SPLASH) {
      // Use POT_A to navigate: upper half = STAY, lower half = PLAY
      int newOption = ((int)smoothA > 2047) ? 0 : 1;

      // Option changed — reset timer
      if (newOption != selectedOption) {
        selectedOption = newOption;
        playTimerActive = false;
        playHeldSince = 0;
      }

      // Start timer when PLAY is selected
      if (selectedOption == 1) {
        if (!playTimerActive) {
          playHeldSince = xTaskGetTickCount();
          playTimerActive = true;
        }

        // Check if held for 5 seconds
        TickType_t elapsed = xTaskGetTickCount() - playHeldSince;
        if (elapsed >= pdMS_TO_TICKS(5000)) {
          if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            resetGame();
            gameState = STARTING;
            songIndex = 0;
            nextNoteTime = 0;
            xSemaphoreGive(gameMutex);
          }
          playStartSound();
          selectedOption = 0;
          playTimerActive = false;
          playHeldSince = 0;
        }
      }

      // Pass selected option to render task
      menuOption= selectedOption;  // called here so it reflects pot movement immediately
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


// ─── Tarea 2: Física del juego ─────────────────────────────
void TaskUpdatePhysics(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();

  for (;;) {
    if(xSemaphoreTake(gameMutex, pdMS_TO_TICKS(5)) == pdTRUE){

      if (gameState!=PLAYING) {
        xSemaphoreGive(gameMutex);
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      if((scoreA>7)||(scoreB>7)){
        gameState = GAME_OVER;
        xSemaphoreGive(gameMutex);
        continue;
      }

      static TickType_t startTime = 0;

      if (roundJustStarted) {
        startTime = xTaskGetTickCount();
        roundJustStarted = false;
      }

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

      TickType_t now = xTaskGetTickCount();
      // Punto / gol
      if ((now - startTime) > pdMS_TO_TICKS(500)) {

        if (ballX < -10 || ballX > SCREEN_WIDTH + 10) {
          if (ballSpeedX > 0) scoreA++; else scoreB++;
          playPoint = true;
          resetBall();
        }
      }
  
      xSemaphoreGive(gameMutex);
  
    }
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000 / 60));  // ~60 Hz física
  }
}

// ─── Tarea 3: Dibujar en pantalla ──────────────────────────

void TaskRenderDisplay(void *pvParameters) {
  for (;;) {
    if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(8)) == pdTRUE) {
      display.clearDisplay();

      if (gameState == SPLASH) {
        drawSplash(menuOption);
        ringSplash();

      } 
      else if (gameState == GAME_OVER) {
        const char* winner = (scoreA > scoreB) ? "A" : "B";
        centerPrint("WINNER IS:", 8, 2);
        centerPrint(winner, 30, 2);
        xSemaphoreGive(gameMutex);
        display.display();
        vTaskDelay(pdMS_TO_TICKS(3000));

        if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          gameState = SPLASH;
          xSemaphoreGive(gameMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // let physics task exit its loop before reset

        if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          resetGame();           // now safe to reset scores
          xSemaphoreGive(gameMutex);
        }
        
        continue;  // skip the display.display() at the bottom, loop again
      } 
      
      else if (gameState == STARTING) {
        display.clearDisplay();
        display.setTextColor(WHITE);
        centerPrint("READY?", 10, 2);
        centerPrint("3", 30, 2);
        display.display();
        vTaskDelay(pdMS_TO_TICKS(1000));

        display.clearDisplay();
        centerPrint("READY?", 10, 2);
        centerPrint("2", 30, 2);
        display.display();
        vTaskDelay(pdMS_TO_TICKS(1000));

        display.clearDisplay();
        centerPrint("READY?", 10, 2);
        centerPrint("1", 30, 2);
        display.display();
        vTaskDelay(pdMS_TO_TICKS(1000));

        display.clearDisplay();
        centerPrint("GO!", 20, 2);
        display.display();
        vTaskDelay(pdMS_TO_TICKS(500));
        xSemaphoreGive(gameMutex);

        if (xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          resetGame();
          gameState = PLAYING;  // ← physics starts only after countdown finishes
          xSemaphoreGive(gameMutex);
        }
        continue;
      }

      else if (gameState == PLAYING) {
        updateScore();
        display.fillRect(PADDLE_PAD, paddleA, PADDLE_W, PADDLE_H, WHITE);
        display.fillRect(SCREEN_WIDTH - PADDLE_PAD - PADDLE_W, paddleB, PADDLE_W, PADDLE_H, WHITE);

        for (int i = 0; i < SCREEN_HEIGHT; i += 6)
          display.drawFastVLine(SCREEN_WIDTH / 2, i, 4, WHITE);

        display.fillRect((int)ballX, (int)ballY, BALL_SIZE, BALL_SIZE, WHITE);

        display.setTextSize(FONT_SIZE);
        display.setTextColor(WHITE);
        String sa = String(scoreA);
        String sb = String(scoreB);
        int wa = sa.length() * 6 * FONT_SIZE;
        display.setCursor(SCREEN_WIDTH / 2 - SCORE_PAD - wa, 4);
        display.print(sa);
        display.setCursor(SCREEN_WIDTH / 2 + SCORE_PAD + 2, 4);
        display.print(sb);
      }

      xSemaphoreGive(gameMutex);
    }

    display.display();  // single call for all states
    vTaskDelay(pdMS_TO_TICKS(1000 / 45));
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
// ─── Tarea 5: NeoPixel (no bloqueante) ───────────────────────
void taskNeoPixel(void *pvParameters) {
  ring.begin();
  //ring.setBrightness(60); // Moderate brightness for battery longevity
  while (true) {
    // Show RED
    if(gameState==SPLASH){

      for(int i=0; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 0, 0));
      for(int i=1; i<NUM_LEDS; i+=2) ring.setPixelColor(i, ring.Color(255, 255, 255));
      ring.show();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    else{

      for(int i=0; i<8 && i<scoreA; i++) ring.setPixelColor(i, ring.Color(0, 255, 0));
      ring.show();
      vTaskDelay(pdMS_TO_TICKS(10));
  
      // Show BLUE
      for(int i=15; i>7 && (15-i)<scoreB; i--) ring.setPixelColor(i, ring.Color(0, 0, 255));
      ring.show();
      vTaskDelay(pdMS_TO_TICKS(10));
    }

  }
}

