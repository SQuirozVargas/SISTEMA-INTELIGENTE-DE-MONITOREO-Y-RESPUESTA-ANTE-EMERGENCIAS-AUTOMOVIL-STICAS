// database.h
#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>

void enviarDatosServidor(String tipo_evento, float latitud, float longitud, int bpm, int direccion);

#endif
