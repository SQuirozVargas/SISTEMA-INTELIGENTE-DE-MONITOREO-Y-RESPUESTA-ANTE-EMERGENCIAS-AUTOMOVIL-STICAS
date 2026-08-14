import cv2
import time
from gpiozero import OutputDevice
 
# Configurar el pin GPIO 17 como salida (Inicia en LOW / 0V por defecto)
# Físicamente es el Pin 11 en el cabezal de la Raspberry Pi
pin_alerta = OutputDevice(17, initial_value=False)
 
# Cargar los modelos de detección
face_cascade = cv2.CascadeClassifier('haarcascade_frontalface_default.xml')
eye_cascade = cv2.CascadeClassifier('haarcascade_eye.xml')
 
print("Iniciando cámara con configuración directa (V4L2, MJPG, 640x480)...")
cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Evitar atasco de fotogramas
 
if not cap.isOpened():
    print("ERROR: No se pudo iniciar la cámara. Revisa la conexión USB.")
    exit()
 
print("Sistema SafeShot Iniciado. Monitoreando fatiga... Presiona Ctrl+C para detener.")
 
# Variables para control de tiempo exacto
# NOTA: este cronometro unico cubre DOS situaciones que se tratan igual:
#   1) Ojos cerrados sostenidos (rostro visible, sin ojos detectados)
#   2) Rostro no detectado sostenido (cabeza caida, giro brusco, mala iluminacion, etc.)
# Ambos casos alimentan el MISMO cronometro y activan la MISMA señal HIGH,
# porque para el ESP32 ambos representan "posible perdida de consciencia del conductor".
tiempo_inicio_alerta = None
TIEMPO_ALARMA_SEGUNDOS = 5.0
 
try:
    while True:
        ret, frame = cap.read()
        if not ret:
            break
 
        # Convertir a escala de grises
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = face_cascade.detectMultiScale(gray, scaleFactor=1.3, minNeighbors=5)
 
        ojos_abiertos_confirmados = False
 
        if len(faces) > 0:
            # Si hay varios rostros en cuadro, nos quedamos con el mas grande
            # (asumiendo que es el conductor, el mas cercano a la camara)
            x, y, w, h = max(faces, key=lambda f: f[2] * f[3])
 
            # Recortar la mitad superior del rostro para buscar los ojos
            roi_ojos = gray[y : y + int(h / 2), x : x + w]
            eyes = eye_cascade.detectMultiScale(roi_ojos, scaleFactor=1.1, minNeighbors=7, minSize=(15, 15))
 
            if len(eyes) > 0:
                ojos_abiertos_confirmados = True
 
            # Dibujar rectangulo del rostro detectado (util para depurar visualmente)
            cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)
 
        if ojos_abiertos_confirmados:
            if tiempo_inicio_alerta is not None:
                print("[+] Ojos abiertos. Cronómetro y alerta reiniciados.")
            tiempo_inicio_alerta = None
            pin_alerta.off()
        else:
            if tiempo_inicio_alerta is None:
                tiempo_inicio_alerta = time.time()
            else:
                tiempo_transcurrido = time.time() - tiempo_inicio_alerta
                motivo = "Ojos cerrados" if len(faces) > 0 else "Rostro no detectado"
                print(f"[*] {motivo}: {tiempo_transcurrido:.1f}s / {TIEMPO_ALARMA_SEGUNDOS}s")
 
                if tiempo_transcurrido >= TIEMPO_ALARMA_SEGUNDOS:
                    pin_alerta.on()
                    print("\n=============================================")
                    print(f"¡ALERTA! POSIBLE SOMNOLENCIA - Motivo: {motivo} - SEÑAL HIGH ENVIADA")
                    print("=============================================\n")
 
        # Ventana de video para verificar visualmente que detecta bien (opcional, quitar en produccion)
        cv2.imshow('Deteccion de Somnolencia', frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
 
        time.sleep(0.05)
 
except KeyboardInterrupt:
    print("\nDeteniendo sistema...")
finally:
    pin_alerta.off()
    if cap is not None:
        cap.release()
    cv2.destroyAllWindows()
    print("Recursos liberados.")
