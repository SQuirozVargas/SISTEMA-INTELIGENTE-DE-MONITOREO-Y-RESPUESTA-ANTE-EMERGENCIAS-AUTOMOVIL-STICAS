# SISTEMA INTELIGENTE DE MONITOREO Y RESPUESTA ANTE EMERGENCIAS AUTOMOVILSTICAS
Sistema embebido basado en ESP32 que monitorea en tiempo real el estado físico del conductor, detectando fatiga, desmayo, somnolencia y anomalías cardíacas, con envío automático de alertas geolocalizadas mediante Telegram y registro en una base de datos accesible por panel web. Incluye además un módulo de botón de pánico para situaciones de riesgo como un posible secuestro.

# Estructura del repositorio
Proyecto_Embebidos/
platformio.ini              # Configuracion del proyecto ESP32 (PlatformIO)
## /src
main.cpp                 # Logica principal del ESP32
database.cpp / .h        # Envio de datos al servidor
telegram.cpp / .h        # Envio de alertas por Telegram
apwifieeprommode.h       # Portal de configuracion WiFi (Access Point)
## servidor/
servidor.py              # Backend Flask (API + conexion a MySQL)
index.html               # Dashboard web de monitoreo


# Nota: 
El script de detección de somnolencia para la Raspberry Pi (deteccion_somnolencia.py) y los archivos de audio del módulo DFPlayer Mini (0001.mp3 a 0009.mp3) se gestionan por separado — ver secciones correspondientes más abajo para su ubicación y configuración.


# Requisitos
Hardware
ESP32 DevKitC
Raspberry Pi (Model B, 2GB RAM o superior) con cámara USB
2 sensores piezoeléctricos (FSR)
Sensor infrarrojo (manos en volante)
Sensor de pulso cardíaco MAX30102
Módulo DFPlayer Mini + parlante + tarjeta microSD
Display de 7 segmentos, 3 LEDs, 2 pulsadores, buzzer
Fuente de alimentación 5V/2A
Software
Visual Studio Code con la extensión PlatformIO
Python 3.x en la Raspberry Pi, con OpenCV y gpiozero
Python 3.x para el servidor, con Flask y mysql-connector-python
MySQL Server
# Instalación y configuración
1. Configurar credenciales (antes de compilar)

Este proyecto requiere un archivo src/secrets.h que no se incluye en el repositorio por seguridad. Créalo con el siguiente contenido, reemplazando los valores por los tuyos.
cpp

2. ESP32 (firmware principal)
Abre la carpeta raíz del proyecto (Proyecto_Embebidos/) en VS Code.
Verifica que la extensión PlatformIO reconozca el platformio.ini.
Conecta el ESP32 por USB.
Compila y sube el firmware:
bash
   pio run --target upload
Abre el monitor serial para verificar el arranque correcto:
bash
   pio device monitor
3. Servidor (Flask + MySQL)
Crea la base de datos y las tablas necesarias en MySQL (conductor, eventos).
Instala las dependencias:
bash
   cd servidor
   pip install flask mysql-connector-python
Ajusta las credenciales de conexión a MySQL en servidor.py (usuario, contraseña, host).
Levanta el servidor:
bash
   python servidor.py
Verifica que la IP mostrada al arrancar coincida con la definida en SERVIDOR_URL de tu secrets.h.
Accede al dashboard en:
   http://127.0.0.1:5000
4. Raspberry Pi (detección de somnolencia)
Instala las dependencias:
bash
   sudo apt update
   sudo apt install python3-opencv python3-gpiozero -y
Descarga los clasificadores Haar Cascade necesarios en la misma carpeta del script:
bash
   wget https://raw.githubusercontent.com/opencv/opencv/master/data/haarcascades/haarcascade_frontalface_default.xml
   wget https://raw.githubusercontent.com/opencv/opencv/master/data/haarcascades/haarcascade_eye.xml
Ejecuta el script:
bash
   python3 deteccion_somnolencia.py
5. Módulo de audio (DFPlayer Mini)
Formatea una tarjeta microSD en FAT32.
Copia en la raíz de la tarjeta los archivos 0001.mp3 a 0009.mp3 correspondientes a cada mensaje de alerta del sistema.
Inserta la tarjeta en el módulo DFPlayer Mini antes de energizar el sistema.
# Seguridad
Este repositorio no incluye credenciales reales (WiFi, tokens de Telegram, API keys, contraseñas de base de datos). 
Cada usuario debe configurar su propio archivo según la plantilla indicada y sus propias credenciales de conexión en servidor.py.
