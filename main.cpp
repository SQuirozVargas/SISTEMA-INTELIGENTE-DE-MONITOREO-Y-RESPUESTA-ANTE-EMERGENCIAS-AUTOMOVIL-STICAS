#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include "apwifieeprommode.h"
#include "telegram.h"
#include "database.h"
#include <EEPROM.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 pulsoSensor;

#define PIN_SDA 16
#define PIN_SCL 17

bool  pulsoSensorConectado = false;
float bpmActual            = 0;
float ultimoBpmValido      = 0;
byte  conteoLatidos        = 0;
const byte RATE_SIZE = 4;
byte  ritmos[RATE_SIZE];
byte  ritmoSpot   = 0;
long  ultimoLatido = 0;
unsigned long tiempoPitidoInicio = 0;
bool          pitidoActivo       = false;
#define DURACION_PITIDO_MS 200
unsigned long tiempoBpmCriticoInicio = 0;

#define PIN_DFPLAYER_RX 18 // Conectado al TX del DFPlayer
#define PIN_DFPLAYER_TX 5  // Conectado al RX del DFPlayer (con resistencia 1k)
#define BPM_MIN            40
#define BPM_MAX            180
#define MAX_VARIACION_BPM  20
#define IR_UMBRAL_DEDO     50000

// Pulsos anormales en reposo
#define BPM_CRITICO_BAJO   45
#define BPM_CRITICO_ALTO  130
#define TIEMPO_BPM_CRITICO_MS 15000

// ================== BOTON DE PANICO (secuestro) ==================
#define PIN_PANICO 15
#define TIEMPO_CANCELAR_PANICO_MS 10000   // mantener 10s para cancelar
#define INTERVALO_MAPEO_MS 60000   // 1 minuto entre cada envio de ubicacion
unsigned long tiempoUltimoMapeo = 0;

volatile bool panicoActivo = false;
unsigned long tiempoBotonPanico = 0;
bool botonPanicoPresionado = false;
bool panicoYaEnviado = false;

// --------- Reconexion WiFi ---------
unsigned long ultimoIntentoReconexion = 0;
#define INTERVALO_RECONEXION_MS 15000   // revisa cada 15 s

// ================== DFPLAYER MINI ==================
DFRobotDFPlayerMini dfPlayer;
bool dfPlayerConectado = false;

#define AUDIO_MANOS_VOLANTE   1   // 0001.mp3
#define AUDIO_DESMAYO         4   // 0002.mp3
#define AUDIO_POSTURA         5   // 0003.mp3
#define AUDIO_SOMNOLENCIA     6   // 0004.mp3
#define AUDIO_PULSO_ANORMAL   7   // 0005.mp3
#define AUDIO_FUGA_GAS        8   // 0006.mp3
#define AUDIO_EMERGENCIA      9   // 0007.mp3
#define AUDIO_CANCELACION     2   // 0008.mp3
#define AUDIO_INICIO          3   // 0009.mp3

void reproducirAudio(int pista) {
  if (!dfPlayerConectado) return;
  dfPlayer.play(pista);
}
// ================== CLAVE DE CANCELACION (modo secuestro) ==================
// Secuencia: C P C P C P C P  (C = boton cancelar pin 19, P = boton panico pin 15)
const int claveSecuencia[8] = {19, 15, 19, 15, 19, 15, 19, 15};
int indiceClave = 0;                    // en que posicion de la secuencia vamos
bool botonCancelarPrevio = false;       // para detectar el flanco del boton 19

// ================== PINES DISPLAY ==================
#define SEG_A 13
#define SEG_B 32
#define SEG_C 14
#define SEG_D 27
#define SEG_E 26
#define SEG_F 25
#define SEG_G 33

// ================== LEDS ==================
#define LED_VERDE    23
#define LED_AMARILLO 22
#define LED_ROJO     21

// ================== BUZZER ==================
#define BUZZER     4
#define LEDC_CANAL 0

// ================== BOTON CANCELAR ==================
#define BTN_CANCELAR 19

// ================== SENSOR SOMNOLENCIA (Raspberry Pi) ==================
#define PIN_SOMNOLENCIA 36
#define TIEMPO_BUZZER_SOMNOLENCIA_MS 6000

// ================== PIEZOS ==================
#define PIN_PIEZO_IZQ     34
#define PIN_PIEZO_DER     35
#define UMBRAL_SENTADO_ON     1000
#define UMBRAL_SENTADO_OFF    700
#define UMBRAL_DESBALANCE_ON   500
#define UMBRAL_DESBALANCE_OFF  350
#define TAMANO_FILTRO_PIEZO 8

// ================== SENSOR MANOS EN VOLANTE (IR) ==================
#define PIN_MANOS_VOLANTE 39


unsigned long tiempoUltimoAvisoGas = 0;
bool gasDetectadoAntes = false;
// ================== TIEMPOS ==================
#define TIEMPO_SIN_MANOS_MS             8000
#define TIEMPO_BUZZER_MS                8000
#define TIEMPO_INCLINACION_ALERTA_MS    8000
#define TIEMPO_INCLINACION_CON_MANOS_MS 30000

// ================== GEOLOCALIZACION GOOGLE MAPS ==================
const char* GOOGLE_GEOLOCATION_URL = "TU_GOOGLE_API_KEY";

double gpsLat = 0.0, gpsLng = 0.0;
bool   gpsValido = false;

void obtenerUbicacionWiFi() {
  gpsValido = false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[UBICACION] Sin WiFi, no se puede geolocalizar.");
    return;
  }
  Serial.println("[UBICACION] Escaneando redes WiFi cercanas...");
  int redesEncontradas = WiFi.scanNetworks();
  if (redesEncontradas < 2) {
    Serial.println("[UBICACION] Muy pocas redes visibles.");
    WiFi.scanDelete();
    return;
  }
  StaticJsonDocument<2048> docEnvio;
  JsonArray puntosAcceso = docEnvio.createNestedArray("wifiAccessPoints");
  int maxRedes = min(redesEncontradas, 10);
  for (int i = 0; i < maxRedes; i++) {
    JsonObject punto = puntosAcceso.createNestedObject();
    punto["macAddress"]     = WiFi.BSSIDstr(i);
    punto["signalStrength"] = WiFi.RSSI(i);
    punto["channel"]        = WiFi.channel(i);
  }
  WiFi.scanDelete();
  String jsonEnvio;
  serializeJson(docEnvio, jsonEnvio);
  HTTPClient http;
  http.begin(GOOGLE_GEOLOCATION_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  int httpCode = http.POST(jsonEnvio);
  if (httpCode == 200) {
    String respuesta = http.getString();
    StaticJsonDocument<512> docRespuesta;
    DeserializationError error = deserializeJson(docRespuesta, respuesta);
    if (!error) {
      gpsLat    = docRespuesta["location"]["lat"];
      gpsLng    = docRespuesta["location"]["lng"];
      gpsValido = true;
      Serial.print("[UBICACION] Obtenida con Google: ");
      Serial.print(gpsLat, 6); Serial.print(", "); Serial.println(gpsLng, 6);
    } else {
      Serial.println("[UBICACION] Error parseando respuesta de Google.");
    }
  } else {
    Serial.println("[UBICACION] Error consultando Google API: " + String(httpCode));
    if (httpCode > 0) {
      Serial.println("[UBICACION] Detalle: " + http.getString());
    }
  }
  http.end();
}

// ================== MAQUINA DE ESTADOS ==================
enum EstadoSistema { VACIO, SENTADO_NORMAL, INCLINADO, ADVERTENCIA_MANOS, EMERGENCIA };
EstadoSistema estadoActual = VACIO;

hw_timer_t  *timerSensores = NULL;
portMUX_TYPE timerMux      = portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR onTimerSensores() {}

int    valorPiezoIzq = 0, valorPiezoDer = 0, valorDelta = 0;
bool   personaSentada = false, inclinacionActiva = false, manosEnVolante = false;
String dirInclinacion = "Normal";

int  historialPiezoIzq[TAMANO_FILTRO_PIEZO] = {0};
int  historialPiezoDer[TAMANO_FILTRO_PIEZO] = {0};
int  idxFiltroPiezo = 0;
long sumaPiezoIzq = 0, sumaPiezoDer = 0;

unsigned long tiempoSinManos = 0, tiempoInclinacion = 0, tiempoBuzzerInicio = 0;
unsigned long tiempoBuzzerInclinacion = 0, tiempoInclinacionConManos = 0;
bool          buzzerAdvertencia = false, buzzerInclinacionActivo = false, avisoPosturaDado = false;
String        tipoEmergencia = "";

bool          inclinacionIniciada = false;
unsigned long tiempoSinInclinacion = 0;
#define TIEMPO_FIN_INCLINACION_MS   500
unsigned long tiempoSinPresencia = 0;
#define TIEMPO_FIN_PRESENCIA_MS     500

unsigned long tiempoSomnolenciaInicio = 0, tiempoBuzzerSomnolencia = 0;
bool          buzzerSomnolenciaActivo = false;

volatile bool emergenciaActiva = false;
volatile bool cancelado        = false;
int           contador         = 9;
unsigned long ultimoTick       = 0;

const bool numeros[10][7] = {
  {0,0,0,0,0,0,1},{1,0,0,1,1,1,1},{0,0,1,0,0,1,0},{0,0,0,0,1,1,0},{1,0,0,1,1,0,0},
  {0,1,0,0,1,0,0},{0,1,0,0,0,0,0},{0,0,0,1,1,1,1},{0,0,0,0,0,0,0},{0,0,0,0,1,0,0}
};

void mostrarNumero(int n) {
  if (n < 0 || n > 9) return;
  digitalWrite(SEG_A, numeros[n][0]); digitalWrite(SEG_B, numeros[n][1]);
  digitalWrite(SEG_C, numeros[n][2]); digitalWrite(SEG_D, numeros[n][3]);
  digitalWrite(SEG_E, numeros[n][4]); digitalWrite(SEG_F, numeros[n][5]);
  digitalWrite(SEG_G, numeros[n][6]);
}

void apagarDisplay() {
  digitalWrite(SEG_A,HIGH); digitalWrite(SEG_B,HIGH); digitalWrite(SEG_C,HIGH);
  digitalWrite(SEG_D,HIGH); digitalWrite(SEG_E,HIGH); digitalWrite(SEG_F,HIGH);
  digitalWrite(SEG_G,HIGH);
}

void IRAM_ATTR ISR_cancelar() {
  if (emergenciaActiva) { cancelado = true; emergenciaActiva = false; }
}

// El buzzer se usa UNICAMENTE para el conteo regresivo y el envio de alerta.
// El resto de avisos se dan por voz con el DFPlayer.
void sonidoEmergency() {
  ledcWriteTone(LEDC_CANAL, 500); delay(200); ledcWriteTone(LEDC_CANAL,0); delay(250);
  ledcWriteTone(LEDC_CANAL,1000); delay(200); ledcWriteTone(LEDC_CANAL,0); delay(250);
  ledcWriteTone(LEDC_CANAL,2000); delay(500); ledcWriteTone(LEDC_CANAL,0);
}

bool hayInternetReal() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient c; c.setTimeout(2000);
  bool ok = c.connect("api.telegram.org", 443);
  c.stop(); return ok;
}

String traducirEvento(String tipo) {
  if (tipo == "MANOS_FUERA_VOLANTE") return "El conductor retiro las manos del volante";
  if (tipo == "DESMAYO_DERECHA")     return "Posible desmayo: el conductor se inclino hacia la DERECHA";
  if (tipo == "DESMAYO_IZQUIERDA")   return "Posible desmayo: el conductor se inclino hacia la IZQUIERDA";
  if (tipo == "SOMNOLENCIA_DETECTADA") return "Posible somnolencia: el conductor cerro los ojos o se perdio de la camara";
  if (tipo == "BPM_ANORMAL") return "Alerta: se detecto un ritmo cardiaco anormal en el conductor";
  return tipo;
}

void enviarAlerta() {
  obtenerUbicacionWiFi();

  String ubicacion = gpsValido
    ? "https://maps.google.com/?q=" + String(gpsLat,6) + "," + String(gpsLng,6)
    : "Ubicacion no disponible";

  String mensajeClaro = traducirEvento(tipoEmergencia);
  String pulsoStr = (bpmActual > 0) ? String((int)bpmActual) + " BPM" : "No detectado";

  String msgTelegram = "🚨 ALERTA DE EMERGENCIA 🚨\n\n"
                      + mensajeClaro + "\n\n"
                      + "💓 Pulso: " + pulsoStr + "\n"
                      + "📍 Ubicacion: " + ubicacion;

  int direccionNum = 0;
  if (dirInclinacion == "DERECHA") direccionNum = 1;
  else if (dirInclinacion == "IZQUIERDA") direccionNum = -1;

  Serial.println("[ALERTA] Verificando conectividad...");
  if (hayInternetReal()) {
    Serial.println("[ALERTA] Enviando por WiFi/Telegram...");
    enviarAlertaTelegram(msgTelegram);
    enviarDatosServidor(tipoEmergencia, gpsLat, gpsLng, (int)bpmActual, direccionNum);
  } else {
    Serial.println("[ALERTA] Sin internet. No se pudo notificar externamente.");
  }
}

int filtrarPiezo(int nuevaLectura, int* historial, long* suma) {
  *suma -= historial[idxFiltroPiezo];
  historial[idxFiltroPiezo] = nuevaLectura;
  *suma += nuevaLectura;
  return *suma / TAMANO_FILTRO_PIEZO;
}

void leerSensores() {
  int rawIzq = analogRead(PIN_PIEZO_IZQ);
  int rawDer = analogRead(PIN_PIEZO_DER);
  valorPiezoIzq = filtrarPiezo(rawIzq, historialPiezoIzq, &sumaPiezoIzq);
  valorPiezoDer = filtrarPiezo(rawDer, historialPiezoDer, &sumaPiezoDer);
  idxFiltroPiezo = (idxFiltroPiezo + 1) % TAMANO_FILTRO_PIEZO;
  valorDelta = valorPiezoDer - valorPiezoIzq;

  if (valorPiezoIzq > UMBRAL_SENTADO_ON || valorPiezoDer > UMBRAL_SENTADO_ON) personaSentada = true;
  else if (valorPiezoIzq < UMBRAL_SENTADO_OFF && valorPiezoDer < UMBRAL_SENTADO_OFF) personaSentada = false;

  if (valorDelta > UMBRAL_DESBALANCE_ON) { inclinacionActiva = true; dirInclinacion = "DERECHA"; }
  else if (valorDelta < -UMBRAL_DESBALANCE_ON) { inclinacionActiva = true; dirInclinacion = "IZQUIERDA"; }
  else if (abs(valorDelta) < UMBRAL_DESBALANCE_OFF) { inclinacionActiva = false; dirInclinacion = "Normal"; }

  manosEnVolante = (digitalRead(PIN_MANOS_VOLANTE) == LOW);
}


void resetFiltroPulso() {
  bpmActual       = 0;
  ultimoBpmValido = 0;
  conteoLatidos   = 0;
  ritmoSpot       = 0;
  for (byte i = 0; i < RATE_SIZE; i++) ritmos[i] = 0;
}

void leerPulso() {
  if (!pulsoSensorConectado) return;

  long irValue = pulsoSensor.getIR();

  if (irValue < IR_UMBRAL_DEDO) {
    resetFiltroPulso();
    return;
  }

  if (checkForBeat(irValue)) {
    long delta = millis() - ultimoLatido;
    ultimoLatido = millis();

    float bpm = 60.0 / (delta / 1000.0);

    if (bpm > BPM_MIN && bpm < BPM_MAX) {
      if (ultimoBpmValido == 0 || abs(bpm - ultimoBpmValido) < MAX_VARIACION_BPM) {
        ultimoBpmValido = bpm;

        ritmos[ritmoSpot++] = (byte)bpm;
        ritmoSpot %= RATE_SIZE;
        if (conteoLatidos < RATE_SIZE) conteoLatidos++;

        int suma = 0;
        for (byte x = 0; x < conteoLatidos; x++) suma += ritmos[x];
        bpmActual = suma / conteoLatidos;
      }
    }
  }
}

void vigilarPulso() {
  if (estadoActual == VACIO || estadoActual == EMERGENCIA) {
    tiempoBpmCriticoInicio = 0;
    return;
  }

  bool bpmAnormal = (bpmActual > 0) && (bpmActual < BPM_CRITICO_BAJO || bpmActual > BPM_CRITICO_ALTO);

  if (!bpmAnormal) {
    tiempoBpmCriticoInicio = 0;
    return;
  }

  if (tiempoBpmCriticoInicio == 0) tiempoBpmCriticoInicio = millis();

  if (millis() - tiempoBpmCriticoInicio >= TIEMPO_BPM_CRITICO_MS) {
    reproducirAudio(AUDIO_PULSO_ANORMAL);
    Serial.println("EMERGENCIA: BPM anormal detectado (" + String(bpmActual) + ")");

    tipoEmergencia          = "BPM_ANORMAL";
    emergenciaActiva        = true;
    cancelado               = false;
    contador                = 9;
    estadoActual            = EMERGENCIA;
    tiempoBpmCriticoInicio  = 0;
  }
}

void vigilarSomnolencia() {
  if (estadoActual == VACIO || estadoActual == EMERGENCIA) {
    tiempoSomnolenciaInicio = 0; buzzerSomnolenciaActivo = false; return;
  }
  bool alertaCamara = (digitalRead(PIN_SOMNOLENCIA) == HIGH);
  if (!alertaCamara) {
    tiempoSomnolenciaInicio = 0; buzzerSomnolenciaActivo = false; return;
  }
  if (tiempoSomnolenciaInicio == 0) tiempoSomnolenciaInicio = millis();
  if (!buzzerSomnolenciaActivo) {
    buzzerSomnolenciaActivo = true;
    tiempoBuzzerSomnolencia = millis();
    reproducirAudio(AUDIO_SOMNOLENCIA);
    Serial.println("ADVERTENCIA: Somnolencia detectada por camara");
  }
  if (millis() - tiempoBuzzerSomnolencia >= TIEMPO_BUZZER_SOMNOLENCIA_MS) {
    tipoEmergencia = "SOMNOLENCIA_DETECTADA";
    emergenciaActiva = true; cancelado = false; contador = 9;
    estadoActual = EMERGENCIA;
    buzzerSomnolenciaActivo = false; tiempoSomnolenciaInicio = 0;
  }
}



void enviarAlertaPanico() {
  Serial.println("[PANICO] Enviando ubicacion a contactos...");
  obtenerUbicacionWiFi();

  String ubicacion = gpsValido
    ? "https://maps.google.com/?q=" + String(gpsLat,6) + "," + String(gpsLng,6)
    : "Ubicacion no disponible";

  String msg = "🆘 ALERTA DE PANICO 🆘\n\n"
               "El conductor activo el boton de emergencia.\n"
               "Posible situacion de riesgo.\n\n"
               "📍 Ubicacion: " + ubicacion;

  if (hayInternetReal()) {
    enviarAlertaTelegram(msg);
    enviarDatosServidor("PANICO_SECUESTRO", gpsLat, gpsLng, (int)bpmActual, 0);
  }

  tiempoUltimoMapeo = millis();   // arranca el conteo del minuto
}

void enviarMapeoPanico() {
  Serial.println("[MAPEO] Actualizando ubicacion...");
  obtenerUbicacionWiFi();

  String ubicacion = gpsValido
    ? "https://maps.google.com/?q=" + String(gpsLat,6) + "," + String(gpsLng,6)
    : "Ubicacion no disponible";

  String msg = "📡 SEGUIMIENTO EN CURSO 📡\n\n"
               "Ubicacion actualizada del vehiculo:\n\n"
               "📍 " + ubicacion;

  if (hayInternetReal()) {
    enviarAlertaTelegram(msg);
    enviarDatosServidor("PANICO_SEGUIMIENTO", gpsLat, gpsLng, (int)bpmActual, 0);
  }
}

void enviarCancelacionPanico() {
  Serial.println("[PANICO] Notificando cancelacion...");
  obtenerUbicacionWiFi();

  String ubicacion = gpsValido
    ? "https://maps.google.com/?q=" + String(gpsLat,6) + "," + String(gpsLng,6)
    : "Ubicacion no disponible";

  String msg = "✅ ALERTA CANCELADA ✅\n\n"
               "El conductor desactivo la alerta de emergencia.\n"
               "La situacion parece estar bajo control.\n\n"
               "📍 Ultima ubicacion: " + ubicacion;

  if (hayInternetReal()) {
    enviarAlertaTelegram(msg);
    enviarDatosServidor("PANICO_CANCELADO", gpsLat, gpsLng, (int)bpmActual, 0);
  }
}

void leerClaveCancelacion() {
  // Detecta pulsaciones nuevas de cada boton (flanco de bajada)
  bool cancelarAhora = (digitalRead(BTN_CANCELAR) == LOW);
  bool panicoAhora   = (digitalRead(PIN_PANICO) == LOW);

  int botonPresionado = 0;   // 0 = ninguno

  // Flanco del boton cancelar
  if (cancelarAhora && !botonCancelarPrevio) botonPresionado = BTN_CANCELAR;
  // Flanco del boton panico
  if (panicoAhora && !botonPanicoPresionado) botonPresionado = PIN_PANICO;

  botonCancelarPrevio = cancelarAhora;
  botonPanicoPresionado = panicoAhora;

  if (botonPresionado == 0) return;   // no se presiono nada nuevo

  // Compara con la tecla esperada en la secuencia
  if (botonPresionado == claveSecuencia[indiceClave]) {
    indiceClave++;
    Serial.print("[CLAVE] Correcta ");
    Serial.print(indiceClave); Serial.println("/8");

    if (indiceClave >= 8) {
      // Clave completa: cancelar el secuestro
      Serial.println("[CLAVE] Secuencia completa. Cancelando modo secuestro.");
      panicoActivo = false;
      indiceClave = 0;
      enviarCancelacionPanico();
      // resetea la maquina de estados para que todo revida limpio
      estadoActual = VACIO;
    }
  } else {
    // Tecla incorrecta: reinicia la secuencia
    if (indiceClave > 0) Serial.println("[CLAVE] Incorrecta. Reiniciando secuencia.");
    indiceClave = 0;
    // si la tecla equivocada coincide con el primer paso, cuenta como inicio
    if (botonPresionado == claveSecuencia[0]) indiceClave = 1;
  }
}
void vigilarPanico() {
  bool presionado = (digitalRead(PIN_PANICO) == LOW);

  // --- DISPARO: un click activa el panico (solo en modo normal) ---
  if (presionado && !botonPanicoPresionado && !panicoActivo) {
    panicoActivo = true;
    panicoYaEnviado = false;
    indiceClave = 0;                    // arranca la secuencia de clave en cero
    Serial.println("[PANICO] Activado - modo secuestro. Sistema centrado en el secuestro.");
  }

  // --- PRIMER ENVIO ---
  if (panicoActivo && !panicoYaEnviado) {
    panicoYaEnviado = true;
    enviarAlertaPanico();
  }

  // --- MAPEO CADA MINUTO mientras siga activo ---
  if (panicoActivo && panicoYaEnviado) {
    if (millis() - tiempoUltimoMapeo >= INTERVALO_MAPEO_MS) {
      tiempoUltimoMapeo = millis();
      enviarMapeoPanico();
    }
  }

  // --- LECTURA DE LA CLAVE (solo si el panico esta activo) ---
  if (panicoActivo) {
    leerClaveCancelacion();
  } else {
    botonPanicoPresionado = presionado;   // mantiene el flanco actualizado en modo normal
  }
}



void actualizarEstado() {
  switch (estadoActual) {
    case VACIO:
      digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_ROJO, LOW);
      apagarDisplay();
      tiempoSinManos = 0; tiempoSinPresencia = 0; tiempoInclinacion = 0; tiempoSinInclinacion = 0;
      tiempoInclinacionConManos = 0;
      inclinacionIniciada = false; buzzerAdvertencia = false; buzzerInclinacionActivo = false; avisoPosturaDado = false;
      if (personaSentada) estadoActual = SENTADO_NORMAL;
      break;

    case SENTADO_NORMAL:
      digitalWrite(LED_VERDE, HIGH); digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_ROJO, LOW);
      apagarDisplay();
      avisoPosturaDado = false;
      if (!personaSentada) {
        if (tiempoSinPresencia == 0) tiempoSinPresencia = millis();
        if (millis() - tiempoSinPresencia >= TIEMPO_FIN_PRESENCIA_MS) { tiempoSinPresencia = 0; estadoActual = VACIO; }
        break;
      }
      tiempoSinPresencia = 0;
      if (inclinacionActiva) {
        tiempoSinInclinacion = 0;
        if (!inclinacionIniciada) { inclinacionIniciada = true; tiempoInclinacion = millis(); buzzerInclinacionActivo = false; }
        estadoActual = INCLINADO;
        break;
      }
      if (inclinacionIniciada) {
        if (tiempoSinInclinacion == 0) tiempoSinInclinacion = millis();
        if (millis() - tiempoSinInclinacion >= TIEMPO_FIN_INCLINACION_MS) {
          inclinacionIniciada = false; tiempoSinInclinacion = 0; tiempoInclinacion = 0; buzzerInclinacionActivo = false;
        }
      }
      if (!manosEnVolante) {
        if (tiempoSinManos == 0) tiempoSinManos = millis();
        if (millis() - tiempoSinManos >= TIEMPO_SIN_MANOS_MS) estadoActual = ADVERTENCIA_MANOS;
      } else tiempoSinManos = 0;
      break;

    case INCLINADO:
      digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, HIGH); digitalWrite(LED_ROJO, LOW);
      if (!personaSentada) {
        if (tiempoSinPresencia == 0) tiempoSinPresencia = millis();
        if (millis() - tiempoSinPresencia >= TIEMPO_FIN_PRESENCIA_MS) {
          tiempoSinPresencia = 0; tiempoInclinacion = 0; inclinacionIniciada = false; tiempoSinInclinacion = 0;
          buzzerInclinacionActivo = false; avisoPosturaDado = false; tiempoInclinacionConManos = 0;
          estadoActual = VACIO;
        }
        break;
      }
      tiempoSinPresencia = 0;
      if (!inclinacionActiva) {
        tiempoInclinacion = 0; inclinacionIniciada = false; buzzerInclinacionActivo = false; avisoPosturaDado = false;
        tiempoInclinacionConManos = 0; tiempoSinManos = 0;
        estadoActual = SENTADO_NORMAL; break;
      }
      if (!manosEnVolante && millis() - tiempoInclinacion >= TIEMPO_INCLINACION_ALERTA_MS) {
        if (!buzzerInclinacionActivo) {
          buzzerInclinacionActivo = true; tiempoBuzzerInclinacion = millis();
          reproducirAudio(AUDIO_DESMAYO);
          Serial.println("ADVERTENCIA: Inclinado sin manos -> posible desmayo " + dirInclinacion);
        }
        if (millis() - tiempoBuzzerInclinacion >= TIEMPO_BUZZER_MS) {
          buzzerInclinacionActivo = false;
          tipoEmergencia = "DESMAYO_" + dirInclinacion;
          emergenciaActiva = true; cancelado = false; contador = 9; estadoActual = EMERGENCIA;
        }
      }
      if (manosEnVolante) {
        buzzerInclinacionActivo = false;
        tiempoInclinacion = millis();
        if (tiempoInclinacionConManos == 0) tiempoInclinacionConManos = millis();
        if (!avisoPosturaDado && millis() - tiempoInclinacionConManos >= TIEMPO_INCLINACION_CON_MANOS_MS) {
          avisoPosturaDado = true; tiempoInclinacionConManos = millis();
          reproducirAudio(AUDIO_POSTURA);
          Serial.println("POSTURA: Inclinado con manos " + dirInclinacion);
        }
        if (avisoPosturaDado && millis() - tiempoInclinacionConManos >= TIEMPO_INCLINACION_CON_MANOS_MS) avisoPosturaDado = false;
      } else tiempoInclinacionConManos = 0;
      break;

    case ADVERTENCIA_MANOS:
      digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, HIGH); digitalWrite(LED_ROJO, LOW);
      if (!personaSentada) { estadoActual = VACIO; break; }

      // si aparece inclinacion, escala al estado mas severo y reclasifica el evento
      if (inclinacionActiva) {
        inclinacionIniciada = true;
        tiempoInclinacion = buzzerAdvertencia ? tiempoBuzzerInicio : millis();
        buzzerAdvertencia = false;
        buzzerInclinacionActivo = false;
        tiempoInclinacionConManos = 0;
        estadoActual = INCLINADO;
        break;
      }

      if (manosEnVolante) {
        tiempoSinManos = 0; buzzerAdvertencia = false;
        estadoActual = SENTADO_NORMAL; break;
      }
      if (!buzzerAdvertencia) {
        buzzerAdvertencia = true; tiempoBuzzerInicio = millis();
        reproducirAudio(AUDIO_MANOS_VOLANTE);
        Serial.println("ADVERTENCIA: Sin manos en volante");
      }
      if (millis() - tiempoBuzzerInicio >= TIEMPO_BUZZER_MS) {
        tipoEmergencia = "MANOS_FUERA_VOLANTE";
        emergenciaActiva = true; cancelado = false; contador = 9; estadoActual = EMERGENCIA;
      }
      break;

    case EMERGENCIA:
      digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, HIGH); digitalWrite(LED_ROJO, HIGH);

      // apaga el pitido del tick anterior sin bloquear el loop
      if (pitidoActivo && millis() - tiempoPitidoInicio >= DURACION_PITIDO_MS) {
        ledcWriteTone(LEDC_CANAL, 0);
        pitidoActivo = false;
      }

      if (cancelado) {
        ledcWriteTone(LEDC_CANAL, 0); pitidoActivo = false;
        apagarDisplay();
        digitalWrite(LED_VERDE, HIGH); digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_ROJO, LOW);
        reproducirAudio(AUDIO_CANCELACION);
        Serial.println("Emergencia cancelada.");
        cancelado = false; tiempoSinManos = 0; tiempoSinPresencia = 0; tiempoInclinacion = 0; tiempoSinInclinacion = 0;
        tiempoInclinacionConManos = 0;
        inclinacionIniciada = false; buzzerAdvertencia = false; buzzerInclinacionActivo = false; avisoPosturaDado = false;
        estadoActual = SENTADO_NORMAL;
        break;
      }

      if (millis() - ultimoTick >= 1000) {
        ultimoTick = millis();
        mostrarNumero(contador);
        Serial.print("Cuenta regresiva: "); Serial.println(contador);

        if (contador == 0) {
          ledcWriteTone(LEDC_CANAL, 0); pitidoActivo = false;
          emergenciaActiva = false;
          apagarDisplay();
          digitalWrite(LED_ROJO, HIGH); digitalWrite(LED_AMARILLO, LOW);
          sonidoEmergency();
          reproducirAudio(AUDIO_EMERGENCIA);
          enviarAlerta();
          Serial.println("Alerta enviada.");
          estadoActual = VACIO;
        } else {
          ledcWriteTone(LEDC_CANAL, 1000);
          tiempoPitidoInicio = millis();
          pitidoActivo = true;
          contador--;
        }
      }
      break;
  }
}

void vigilarReconexionWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - ultimoIntentoReconexion >= INTERVALO_RECONEXION_MS) {
    ultimoIntentoReconexion = millis();
    Serial.println("[WiFi] Sin conexion, reintentando...");
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(SEG_A,OUTPUT); pinMode(SEG_B,OUTPUT); pinMode(SEG_C,OUTPUT);
  pinMode(SEG_D,OUTPUT); pinMode(SEG_E,OUTPUT); pinMode(SEG_F,OUTPUT); pinMode(SEG_G,OUTPUT);
  pinMode(LED_VERDE,OUTPUT); pinMode(LED_AMARILLO,OUTPUT); pinMode(LED_ROJO,OUTPUT);
  pinMode(PIN_PANICO, INPUT_PULLUP);
  ledcSetup(LEDC_CANAL, 1000, 8);
  ledcAttachPin(BUZZER, LEDC_CANAL);

  pinMode(BTN_CANCELAR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_CANCELAR), ISR_cancelar, FALLING);

  pinMode(PIN_PIEZO_IZQ, INPUT);
  pinMode(PIN_PIEZO_DER, INPUT);
  pinMode(PIN_MANOS_VOLANTE, INPUT);
  pinMode(PIN_SOMNOLENCIA, INPUT);

  timerSensores = timerBegin(1, 80, true);
  timerAttachInterrupt(timerSensores, &onTimerSensores, true);
  timerAlarmWrite(timerSensores, 10000, true);
  timerAlarmEnable(timerSensores);

  Wire.begin(PIN_SDA, PIN_SCL);

  if (pulsoSensor.begin(Wire, I2C_SPEED_FAST)) {
    pulsoSensorConectado = true;
    pulsoSensor.setup();
    pulsoSensor.setPulseAmplitudeRed(0x0A);
    pulsoSensor.setPulseAmplitudeGreen(0);
    Serial.println("[PULSO] MAX30102 conectado correctamente.");
  } else {
    Serial.println("[PULSO] MAX30102 no detectado, revisa conexion.");
  }

  // ================== DFPLAYER MINI ==================
  Serial2.begin(9600, SERIAL_8N1, PIN_DFPLAYER_RX, PIN_DFPLAYER_TX);
  delay(500);

  if (dfPlayer.begin(Serial2)) {
    dfPlayerConectado = true;
    dfPlayer.volume(25);              // rango 0 a 30
    dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    Serial.println("[DFPLAYER] Conectado correctamente.");
  } else {
    Serial.println("[DFPLAYER] No detectado, revisa conexion y tarjeta SD.");
  }

  digitalWrite(LED_VERDE, HIGH); digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_ROJO, LOW);
  apagarDisplay();

  intentoconexion(WIFI_SSID, WIFI_PASSWORD);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Conectado.");
  } else {
    Serial.println("[WiFi] No disponible.");
  }

  Serial.println("Sistema listo");
  reproducirAudio(AUDIO_INICIO);
}

void loop() {
  loopAP();
  vigilarReconexionWiFi();
  vigilarPanico();                 // SIEMPRE corre (maneja el modo secuestro y la clave)

  if (!panicoActivo) {
    // Modo normal: todo funciona
    leerSensores();
    leerPulso();
    vigilarPulso();
    vigilarSomnolencia();
    actualizarEstado();
  }

  static unsigned long ultimoReporte = 0;
  if (millis() - ultimoReporte >= 100) {
    ultimoReporte = millis();
    Serial.print("IZQ: ");       Serial.print(valorPiezoIzq);
    Serial.print(" | DER: ");    Serial.print(valorPiezoDer);
    Serial.print(" | DELTA: ");  Serial.print(valorDelta);
    Serial.print(" | Estado: "); Serial.print(
      estadoActual == VACIO            ? "VACIO"    :
      estadoActual == SENTADO_NORMAL   ? "NORMAL"   :
      estadoActual == INCLINADO        ? "INCLINADO_" + dirInclinacion :
      estadoActual == ADVERTENCIA_MANOS? "SIN_MANOS": "EMERGENCIA"
    );
    Serial.print(" | Sentado: "); Serial.print(personaSentada ? "Si" : "No");
    Serial.print(" | Manos: ");   Serial.print(manosEnVolante ? "Si" : "No");
    Serial.print(" | Somnolencia: "); Serial.print(digitalRead(PIN_SOMNOLENCIA) == HIGH ? "Si" : "No");
    Serial.print(" | IncInc: ");  Serial.print(inclinacionIniciada ? "Si" : "No");
    Serial.print(" | BPM: ");     Serial.print(bpmActual);
    Serial.print(" | WiFi: ");    Serial.println(WiFi.status()==WL_CONNECTED ? "OK" : "OFF");
    Serial.print(" | PANICO_PIN: "); Serial.print(digitalRead(PIN_PANICO));
  }
}
