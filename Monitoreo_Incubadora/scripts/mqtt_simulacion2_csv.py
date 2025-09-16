# mqtt_simulacion2.py (CSV + multi-ESP)
import threading, time, json, random, os, csv
from datetime import datetime
from flask import Flask, request, jsonify
from flask_cors import CORS
from pathlib import Path
import paho.mqtt.client as mqtt

app = Flask(__name__)
CORS(app)

DATA_DIR = os.path.join(os.path.dirname(__file__), "data")
os.makedirs(DATA_DIR, exist_ok=True)

# límites file (igual que antes)
limites_path = Path("limites.json")
DEFAULTS = {
    "Tmin": 36, "Tmax": 37, "Ttol": 1,
    "Hmin": 30, "Hmax": 50, "Htol": 5,
    "Imin": 350, "Imax": 1500, "Itol": 100,
    "Omin": 22, "Omax": 80, "Otol": 10,
}


# valores de simulación base (se usan para cada esp; podés variar)
base = {
    "temperatura": 37.0,
    "humedad": 90.0,
    "iluminancia": 1000.0,
    "oxigeno": 90.0
}

def csv_prepend(path, rowdict, fieldnames):
    """Inserta la fila al inicio (simula insert_rows en Excel) sin duplicar headers."""
    if not os.path.exists(path):
        # Crear archivo nuevo con header
        with open(path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerow(rowdict)
        return

    # Leer contenido existente
    with open(path, 'r', newline='', encoding='utf-8') as f:
        lines = f.readlines()

    # Si la primera línea ya es header, no lo agregamos de nuevo
    if lines and lines[0].strip() == ",".join(fieldnames):
        new_content = [lines[0]] + [",".join([str(rowdict.get(fn, "")) for fn in fieldnames]) + "\n"] + lines[1:]
    else:
        new_content = [",".join(fieldnames) + "\n"] + [",".join([str(rowdict.get(fn, "")) for fn in fieldnames]) + "\n"] + lines

    # Reescribir archivo
    with open(path, 'w', newline='', encoding='utf-8') as f:
        f.writelines(new_content)

# MQTT callbacks
def on_connect(client, userdata, flags, rc):
    print("🔌 Conectado al broker MQTT (simulador)")
    client.subscribe("config/limites")
    client.subscribe("sensor/+/datos")

def on_message(client, userdata, msg):
    try:
        if msg.topic.startswith("sensor/") and msg.topic.endswith("/datos"):
            data = json.loads(msg.payload.decode())
            esp = str(data.get("esp", "1"))
            print(f"MQTT recibido de esp {esp}")
            # guardar en CSV por esp
            fieldnames = ["fecha","temperatura","ley_T","humedad","ley_H","iluminancia","ley_I","oxigeno","ley_O"]
            row = {
                "fecha": data.get("fecha"),
                "temperatura": data.get("temperatura"),
                "ley_T": data.get("ley_T"),
                "humedad": data.get("humedad"),
                "ley_H": data.get("ley_H"),
                "iluminancia": data.get("iluminancia"),
                "ley_I": data.get("ley_I"),
                "oxigeno": data.get("oxigeno"),
                "ley_O": data.get("ley_O")
            }
            path = os.path.join(DATA_DIR, f"incubadora_{esp}.csv")
            csv_prepend(path, row, fieldnames)
    except Exception as e:
        print("❌ Error procesando MQTT:", e)

def guardar_nuevos_limites(data):
    esp = str(data.get("esp", "1"))
    all_limits = limites_path.exists() and json.loads(limites_path.read_text()) or {}
    prev = all_limits.get(esp, DEFAULTS)
    nuevos = { k: data.get(k, prev.get(k)) for k in DEFAULTS }
    all_limits[esp] = nuevos
    limites_path.write_text(json.dumps(all_limits, indent=2))
    print(f"📥 Nuevos límites para ESP {esp}: {nuevos}")

def on_message(client, userdata, msg):
    try:
        if msg.topic.startswith("sensor/") and msg.topic.endswith("/datos"):
            data = json.loads(msg.payload.decode())
            esp = str(data.get("esp", "1"))
            print(f"MQTT recibido de esp {esp}")
            # guardar en CSV por esp
            fieldnames = ["fecha","temperatura","ley_T","humedad","ley_H","iluminancia","ley_I","oxigeno","ley_O"]
            row = {
                "fecha": data.get("fecha"),
                "temperatura": data.get("temperatura"),
                "ley_T": data.get("ley_T"),
                "humedad": data.get("humedad"),
                "ley_H": data.get("ley_H"),
                "iluminancia": data.get("iluminancia"),
                "ley_I": data.get("ley_I"),
                "oxigeno": data.get("oxigeno"),
                "ley_O": data.get("ley_O")
            }
            path = os.path.join(DATA_DIR, f"incubadora_{esp}.csv")
            csv_prepend(path, row, fieldnames)

        elif msg.topic == "config/limites":
            data = json.loads(msg.payload.decode())
            guardar_nuevos_limites(data)   # 👈 actualiza limites.json por esp_id
            print("✔️ Limites actualizados vía MQTT:", data)

    except Exception as e:
        print("❌ Error procesando MQTT:", e)


cliente_mqtt = mqtt.Client()
cliente_mqtt.on_connect = on_connect
cliente_mqtt.on_message = on_message
cliente_mqtt.connect("broker.emqx.io", 1883)
cliente_mqtt.loop_start()

@app.route("/limites", methods=["GET"])
def enviar_limites_actuales():
    from flask import request
    esp = request.args.get("esp", "1")
    if limites_path.exists():
        all_limits = json.loads(limites_path.read_text())
        return jsonify(all_limits.get(esp, DEFAULTS)), 200
    return jsonify(DEFAULTS), 200

# simulador multi-ESP
def simular_ruido(valor_anterior, max_variacion):
    return round(valor_anterior + random.uniform(-max_variacion, max_variacion), 2)

def limitar(valor, minimo, maximo):
    return min(max(valor, minimo), maximo)

def simulador_esp(esp_id, interval=10):
    """Publica datos simulados por MQTT para esp_id cada `interval` segundos."""
    # variables locales por esp (pueden partir de base y variar)
    temperatura = base["temperatura"] + random.uniform(-0.5, 0.5)
    humedad = base["humedad"] + random.uniform(-2, 2)
    iluminancia = base["iluminancia"] + random.uniform(-50, 50)
    oxigeno = base["oxigeno"] + random.uniform(-1, 1)
    fieldnames = ["fecha","temperatura","ley_T","humedad","ley_H","iluminancia","ley_I","oxigeno","ley_O"]

    while True:
        esp = str(esp_id)
        # leer límites
        if limites_path.exists():
            with open(limites_path, "r") as f:
                all_limits = json.load(f)
                limites = all_limits.get(esp, DEFAULTS)
        else:
            limites = DEFAULTS

        Tmin, Tmax, Ttol = limites["Tmin"], limites["Tmax"], limites["Ttol"]
        Hmin, Hmax, Htol = limites["Hmin"], limites["Hmax"], limites["Htol"]
        Imin, Imax, Itol = limites["Imin"], limites["Imax"], limites["Itol"]
        Omin, Omax, Otol = limites["Omin"], limites["Omax"], limites["Otol"]

        # simular
        temperatura = limitar(simular_ruido(temperatura, 0.8), Tmin, Tmax)
        humedad = limitar(simular_ruido(humedad, 1.5), Hmin, Hmax)
        iluminancia = limitar(simular_ruido(iluminancia, 20), Imin, Imax)
        oxigeno = limitar(simular_ruido(oxigeno, 0.5), Omin, Omax)

        alarma_T = True if ((temperatura < (Tmin-Ttol)) or (temperatura > (Tmax+Ttol))) else False
        alarma_H = True if (humedad < (Hmin-Htol) or humedad > (Hmax+Htol)) else False
        alarma_I = True if ((iluminancia < (Imin-Itol)) or (iluminancia > (Imax+Itol))) else False
        alarma_O = True if ((oxigeno < (Omin-Otol)) or (oxigeno > (Omax+Otol))) else False

        ley_T = "alta" if (temperatura > Tmax and alarma_T) else "baja" if (temperatura < Tmin and alarma_T) else "-"
        ley_H = "alta" if (humedad > Hmax and alarma_H) else "baja" if (humedad < Hmin and alarma_H) else "-"
        ley_I = "alta" if (iluminancia > Imax and alarma_I) else "baja" if (iluminancia < Imin and alarma_I) else "-"
        ley_O = "alto" if (oxigeno > Omax and alarma_O) else "bajo" if (oxigeno < Omin and alarma_O) else "-"

        fecha = datetime.now().isoformat(timespec='seconds')
        datos = {
            "fecha": fecha,
            "temperatura": round(temperatura, 2),
            "humedad": round(humedad, 2),
            "iluminancia": round(iluminancia, 2),
            "oxigeno": round(oxigeno, 2),
            "Tmax": Tmax, "Tmin":Tmin, "Ttol":Ttol,
            "Hmax":Hmax, "Hmin":Hmin, "Htol":Htol,
            "Imax":Imax, "Imin":Imin, "Itol":Itol,
            "Omax":Omax, "Omin":Omin, "Otol":Otol,
            "alarma_T": alarma_T,
            "alarma_H": alarma_H,
            "alarma_I": alarma_I,
            "alarma_O": alarma_O,
            "ley_T":ley_T,
            "ley_H":ley_H,
            "ley_I":ley_I,
            "ley_O":ley_O,
            "esp": int(esp_id),
        }

        # publicar por MQTT
        cliente_mqtt.publish(f"sensor/{esp}/datos", json.dumps(datos))

        # también escribir en CSV localmente (igual que on_message)
        row = {
            "fecha": datos["fecha"],
            "temperatura": datos["temperatura"],
            "ley_T": datos["ley_T"],
            "humedad": datos["humedad"],
            "ley_H": datos["ley_H"],
            "iluminancia": datos["iluminancia"],
            "ley_I": datos["ley_I"],
            "oxigeno": datos["oxigeno"],
            "ley_O": datos["ley_O"]
        }
        path = os.path.join(DATA_DIR, f"incubadora_{esp}.csv")
        csv_prepend(path, row, fieldnames)

        time.sleep(interval)

def start_simulators(max_esp=39, start=2, interval=10): ##max es EL NUMERO DE INCUBADORA MAS ALTO, NO LA CANTIDAD
    """Lanza simuladores en background para los ESPs start..max_esp (incl). Máx 39."""
    max_esp = min(max_esp, 39)
    for esp in range(start, max_esp+1):
        t = threading.Thread(target=simulador_esp, args=(esp, interval), daemon=True)
        t.start()
    print(f"Simuladores arrancados para ESPs {start}..{max_esp}")

if __name__ == "__main__":
    # arrancar simulación (ajustá max_esp a lo que necesites, por ejemplo 39)
    threading.Thread(target=lambda: start_simulators(max_esp=12, start=7, interval=10), daemon=True).start()
    print("✅ MQTT Simulador iniciado. Escuchando y publicando varios ESPs.")
    app.run(host="0.0.0.0", port=5001)
