// database.cpp
#include "database.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Cambia esta IP por la que apareció en tu servidor
const char* servidorURL = "http://10.173.93.111:5000/emergencia";

void enviarDatosServidor(String tipo_evento, float latitud, float longitud, int bpm, int direccion) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Sin WiFi, no se puede enviar a servidor");
        return;
    }

    HTTPClient http;
    http.begin(servidorURL);
    http.addHeader("Content-Type", "application/json");

    String json = "{";
    json += "\"id_conductor\": 2,";
    json += "\"tipo_evento\": \"" + tipo_evento + "\",";
    json += "\"latitud\": " + String(latitud, 6) + ",";
    json += "\"longitud\": " + String(longitud, 6) + ",";
    json += "\"bpm\": " + String(bpm) + ",";
    json += "\"direccion_inclinacion\": " + String(direccion) + ",";
    json += "\"mensaje\": \"Emergencia detectada por sensor\"";
    json += "}";

    int httpCode = http.POST(json);

    if (httpCode == 200) {
        Serial.println("Datos enviados al servidor correctamente");
    } else {
        Serial.println("Error enviando datos: " + String(httpCode));
    }

    http.end();
}
