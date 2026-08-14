from flask import Flask, request, jsonify, send_file
import mysql.connector
from datetime import datetime

app = Flask(__name__)

print("Servidor iniciando...")

def conectar_db():
    return mysql.connector.connect(
        host="TU_HOST_MSQL",
        user="TU_USUARIO_MSQL",
        password="TU_PASSWORD_MYSQL",
        database="sistema_emergencias"
    )

# ===== ENDPOINT PING =====
@app.route('/ping', methods=['GET'])
def ping():
    return jsonify({"status": "ok", "mensaje": "servidor activo"}), 200

# ===== REGISTRAR EMERGENCIA =====
@app.route('/emergencia', methods=['POST'])
def registrar_emergencia():
    datos = request.json
    try:
        db = conectar_db()
        cursor = db.cursor()
        cursor.callproc('registrar_emergencia', [
            datos.get('id_conductor', 1),
            datos.get('tipo_evento', 'desconocido'),
            datos.get('latitud', 0.0),
            datos.get('longitud', 0.0),
            datos.get('bpm', 0),
            datos.get('direccion_inclinacion', 0),
            datos.get('mensaje', 'Emergencia detectada')
        ])
        db.commit()
        cursor.close()
        db.close()
        print(f"Emergencia registrada: {datos.get('tipo_evento')} - {datetime.now()}")
        return jsonify({"status": "ok", "mensaje": "Emergencia registrada"}), 200
    except Exception as e:
        print("Error:", str(e))
        return jsonify({"status": "error", "mensaje": str(e)}), 500

# ===== OBTENER CONDUCTOR =====
@app.route('/conductor/<int:id>', methods=['GET'])
def obtener_conductor(id):
    try:
        db = conectar_db()
        cursor = db.cursor(dictionary=True)
        cursor.execute("SELECT * FROM conductor WHERE id = %s", (id,))
        conductor = cursor.fetchone()
        cursor.close()
        db.close()
        if conductor:
            return jsonify(conductor), 200
        return jsonify({"status": "error", "mensaje": "Conductor no encontrado"}), 404
    except Exception as e:
        return jsonify({"status": "error", "mensaje": str(e)}), 500

# ===== REGISTRAR NUEVO CONDUCTOR =====
@app.route('/conductor', methods=['POST'])
def registrar_conductor():
    datos = request.json
    try:
        db = conectar_db()
        cursor = db.cursor()
        cursor.execute("""
            INSERT INTO conductor 
            (nombre, apellido, cedula, edad, tipo_sangre, alergias, condiciones_medicas, contacto_emergencia, telefono_emergencia)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
        """, (
            datos.get('nombre'),
            datos.get('apellido'),
            datos.get('cedula'),
            datos.get('edad'),
            datos.get('tipo_sangre'),
            datos.get('alergias'),
            datos.get('condiciones_medicas'),
            datos.get('contacto_emergencia'),
            datos.get('telefono_emergencia')
        ))
        db.commit()
        cursor.close()
        db.close()
        return jsonify({"status": "ok", "mensaje": "Conductor registrado"}), 200
    except Exception as e:
        return jsonify({"status": "error", "mensaje": str(e)}), 500

# ===== OBTENER ULTIMA EMERGENCIA =====
@app.route('/ultima_emergencia', methods=['GET'])
def ultima_emergencia():
    try:
        db = conectar_db()
        cursor = db.cursor(dictionary=True)
        cursor.execute("""
            SELECT c.nombre, c.apellido, c.cedula, c.edad, c.tipo_sangre,
                   c.alergias, c.condiciones_medicas, c.contacto_emergencia,
                   c.telefono_emergencia, e.tipo_evento, e.latitud, e.longitud,
                   e.bpm, e.cancelado, e.timestamp
            FROM eventos e
            JOIN conductor c ON e.id_conductor = c.id
            ORDER BY e.timestamp DESC
            LIMIT 1
        """)
        emergencia = cursor.fetchone()
        cursor.close()
        db.close()
        if emergencia:
            if emergencia.get('timestamp'):
                emergencia['timestamp'] = str(emergencia['timestamp'])
            return jsonify(emergencia), 200
        return jsonify({"status": "error", "mensaje": "No hay emergencias"}), 404
    except Exception as e:
        return jsonify({"status": "error", "mensaje": str(e)}), 500

# ===== HISTORIAL DE EMERGENCIAS =====
@app.route('/historial', methods=['GET'])
def historial():
    try:
        db = conectar_db()
        cursor = db.cursor(dictionary=True)
        cursor.execute("""
            SELECT c.nombre, c.apellido, e.tipo_evento, 
                   e.latitud, e.longitud, e.bpm, e.cancelado, e.timestamp
            FROM eventos e
            JOIN conductor c ON e.id_conductor = c.id
            ORDER BY e.timestamp DESC
            LIMIT 50
        """)
        eventos = cursor.fetchall()
        cursor.close()
        db.close()
        for evento in eventos:
            if evento.get('timestamp'):
                evento['timestamp'] = str(evento['timestamp'])
        return jsonify(eventos), 200
    except Exception as e:
        return jsonify({"status": "error", "mensaje": str(e)}), 500

# ===== INTERFAZ WEB =====
@app.route('/')
def index():
    return send_file('index.html')

print("Arrancando en puerto 5000...")
app.run(host='0.0.0.0', port=5000, debug=True)
