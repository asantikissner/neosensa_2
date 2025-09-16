#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_SHT31.h>
#include <U8g2lib.h>
#include <Wire.h>

#define LED            2
#define SDA_PIN        21
#define SCL_PIN        22
#define PIN_O2_ANALOG  35
#define PIN_LUZ_ANALOG 39
#define PIN_BUZZER 16
#define PIN_BOTON 4

// OLED display
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
  U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN
);

// WiFi credentials
const char* ssid     = "Wifi Lola Erbin "; //CAMBIAR!!
const char* password = "Lolaerbin2"; //CAMBIAR!!

// Server for limits
const char* limits_server_ip   = "10.33.239.240"; //CAMBIAR!!
const int   limits_server_port = 5001;
char server_url[80];

// MQTT broker
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ESP identifier and topic
const int  esp_id     = 1;
String     mqtt_topic = "sensor/1/datos";

// Timing
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 10000;

// Limits
float T_min, T_max, T_tol;
float H_min, H_max, H_tol;
float I_min, I_max, I_tol;
float O_min, O_max, O_tol;


// Alarm flags & legends
bool   alarma_T, alarma_H, alarma_I, alarma_O;
String ley_T, ley_H, ley_I, ley_O;
String fecha;


// Sensor
Adafruit_SHT31 sht31;

void connectWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");
  Serial.print("IP: "); Serial.println(WiFi.localIP());
}

void connectMQTT() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  while (!mqttClient.connected()) {
    String clientId = "ESP32_" + String(esp_id);
    Serial.print("Conectando MQTT...");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" OK");
    } else {
      Serial.print(" Falló rc=");
      Serial.print(mqttClient.state());
      Serial.println(" → reintentando en 1s");
      delay(1000);
    }
  }
}

void cargarLimites() {
  Serial.println("Descargando límites...");
  HTTPClient http;
  snprintf(server_url, sizeof(server_url),
           "http://%s:%d/limites?esp=%d",
           limits_server_ip, limits_server_port, esp_id);
  http.begin(server_url);
  if (http.GET() == HTTP_CODE_OK) {
    String resp = http.getString();
    Serial.print("Respuesta recibida: ");
    Serial.println(resp);
    StaticJsonDocument<512> doc;
    auto err = deserializeJson(doc, resp);
    if (err) {
      Serial.print("JSON error: ");
      Serial.println(err.c_str());
    } else {
      T_min = doc["Tmin"];
      T_max = doc["Tmax"];
      T_tol = doc["Ttol"];
      H_min = doc["Hmin"];
      H_max = doc["Hmax"];
      H_tol = doc["Htol"];
      I_min = doc["Imin"];
      I_max = doc["Imax"];
      I_tol = doc["Itol"];
      O_min = doc["Omin"];
      O_max = doc["Omax"];
      O_tol = doc["Otol"];
      Serial.println("Límites cargados");
    }
  } else {
    /////////////////////////// BORRAR ESTO, ES PARA QUE LE FUNCIONE A LOLA
    T_min = 36;
    T_max = 37;
    T_tol = 1;
    H_min = 30;
    H_max = 50;
    H_tol = 5;
    I_min = 350;
    I_max = 1500;
    I_tol = 100;
    O_min = 32;
    O_max = 80;
    O_tol = 10;
    ////////////////////////////
    Serial.print("HTTP error: ");
    Serial.println(http.GET());
  }
  http.end();
}

// Servidores NTP
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

// Zona horaria de Argentina: UTC -3 horas
const long gmtOffset_sec = -3 * 3600;
const int daylightOffset_sec = 0;

void configurarHora() {
  // Configura NTP y la zona horaria
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  Serial.println("Sincronizando hora via NTP…");
  struct tm timeinfo;
  // Espera hasta que tengamos hora válida
  for (int i = 0; i < 20; ++i) {
    if (getLocalTime(&timeinfo)) {
      Serial.println("Hora sincronizada correctamente");
      break;
    }
    Serial.print(".");
    delay(500);
  }
}

String obtenerTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(); // String vacío si no hay hora aún
  }
  char buf[25];
  // Formato: 2025-06-20T22:15:30
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buf);
}

bool publicarLectura(float t, float h, float l, float o) {
  // Verificar conexión MQTT
  Serial.print("¿MQTT conectado? ");
  Serial.println(mqttClient.connected() ? "✅ Sí" : "❌ No");
  if (!mqttClient.connected()) {
    Serial.println("🔌 Intentando reconectar MQTT...");
    connectMQTT();
  }
  Serial.print("Estado MQTT: ");
  Serial.println(mqttClient.state());

  // Construir JSON
  StaticJsonDocument<512> payload;
  payload["fecha"]   = fecha;
  payload["temperatura"] = t;
  payload["humedad"]     = h;
  payload["iluminancia"] = l;
  payload["oxigeno"]     = o;
  payload["Tmax"]=T_max;
  payload["Tmin"]=T_min;
  payload["Ttol"]=T_tol;
  payload["Hmax"]=H_max;
  payload["Hmin"]=H_min;
  payload["Htol"]=H_tol;
  payload["Imax"]=I_max;
  payload["Imin"]=I_min;
  payload["Itol"]=I_tol;
  payload["Omax"]=O_max;
  payload["Omin"]=O_min;
  payload["Otol"]=O_tol;
  payload["alarma_T"] = alarma_T;
  payload["alarma_H"] = alarma_H;
  payload["alarma_I"] = alarma_I;
  payload["alarma_O"] = alarma_O;
  payload["ley_T"] = ley_T;
  payload["ley_H"] = ley_H;
  payload["ley_I"] = ley_I;
  payload["ley_O"] = ley_O;
  payload["esp"] = esp_id;
  

  char bufJson[512];
  size_t n = serializeJson(payload, bufJson);
  Serial.print("Tamaño del payload: ");
  Serial.println(n);

  // Debug
  Serial.print("➡️ Topic: "); Serial.println(mqtt_topic);
  Serial.print("➡️ Payload: "); Serial.println(bufJson);

  // Publicar
  bool ok = mqttClient.publish(mqtt_topic.c_str(), bufJson, n);
  if (ok) {
    Serial.println("✅ Publicación exitosa");
  } else {
    Serial.println("❌ Falló la publicación MQTT");
  }
  return ok;
}

// Variables de estado
bool silenced        = false;
unsigned long silenceStart = 0;
bool prevButtonState = HIGH;  // Partimos asumiendo botón no presionado (pull‑up)

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(PIN_BOTON, INPUT_PULLUP);  // Botón como entrada
  pinMode(PIN_BUZZER, OUTPUT);        // Buzzer como salida

  connectWiFi();
  configurarHora();

  // Tópico estático para tu ESP
  mqtt_topic = "sensor/" + String(esp_id) + "/datos";

  connectMQTT();
  cargarLimites();

  // Init sensor y OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!sht31.begin(0x44)) {
    Serial.println("No detect SHT31");
    while (1) delay(1);
  }
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 12, "OLED OK!");
  display.sendBuffer();
}

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  unsigned long now = millis();
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  float l = analogRead(PIN_LUZ_ANALOG) * (3.3 / 4095.0);
  float o = analogRead(PIN_O2_ANALOG)  * (3.3 / 4095.0);

  Serial.println(I_min);
  Serial.println(I_max);

  alarma_T = ((t < T_min - T_tol) || (t > T_max + T_tol));
  if (alarma_T && (t < T_min - T_tol)){
    ley_T="baja";
  } else if (alarma_T && (t > T_max + T_tol)){
    ley_T="alta";
  }else {ley_T="-";};

  alarma_H = ((h < H_min - H_tol) || (h > H_max + H_tol));
  if (alarma_H && (h < H_min - H_tol)){
    ley_H="baja";
  } else if (alarma_H && (h > H_max + H_tol)){
    ley_H="alta";
  }else {ley_H="-";};

  alarma_I = ((l < I_min - I_tol) || (l > I_max + I_tol));
  if (alarma_I && (l < I_min - I_tol)){
    ley_I="baja";
  } else if (alarma_I && (l > I_max + I_tol)){
    ley_I="alta";
  }else {ley_I="-";};

  alarma_O = ((o < O_min - O_tol) || (o > O_max + O_tol));
  if (alarma_O && (o < O_min - O_tol)){
    ley_O="bajo";
  } else if (alarma_O && (o > O_max + O_tol)){
    ley_O="alto";
  }else {ley_O="-";};

  fecha = obtenerTimestamp();
  Serial.print("Diff (ms): ");
  Serial.println(now - lastSend);

  if (alarma_H==true || now - lastSend >= SEND_INTERVAL) {
    bool ok = publicarLectura(t, h, l, o);
    if (ok) {
      lastSend = now;
    }
     // ———————— actualizar OLED ————————
    char buf[32];
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB12_tr);
    //CALIBRAR LOS ANALOGICOS (AHORA ESTAN EN VOLTS)
    snprintf(buf, sizeof(buf), "Temp: %.1f °C", t);
    display.drawStr(0,  14, buf);
    snprintf(buf, sizeof(buf), "Hum: %.1f %%", h);
    display.drawStr(0, 28, buf);
    snprintf(buf, sizeof(buf), "Luz: %.2f V", l);
    display.drawStr(0, 42, buf);
    snprintf(buf, sizeof(buf), "O2:  %.2f V", o);
    display.drawStr(0, 56, buf);
    display.sendBuffer();
  }

  delay(100);  // suaviza el ciclo

  static unsigned long lastLimitsCheck = 0;
  const unsigned long LIMITS_UPDATE_INTERVAL = 1000;  // 1 s

  if (now - lastLimitsCheck >= LIMITS_UPDATE_INTERVAL) {
    cargarLimites();
    lastLimitsCheck = now;
  }

  bool currButton = digitalRead(PIN_BOTON);
  // Detectar “bajón” en el botón (flanco de subida desde HIGH a LOW)
  if (prevButtonState == HIGH && currButton == LOW) {
    // Si pulsa y estamos en alerta y NO estamos ya en silencio, arrancamos 60s de silencio
    if ( (h < H_min - H_tol || h > H_max + H_tol) && !silenced ) {
      silenced = true;
      silenceStart = now;
    }
  }
  prevButtonState = currButton;
  const unsigned long SILENCE_DURATION = 60000; 
  // Lógica del buzzer
  bool alerta = (h < H_min - H_tol) || (h > H_max + H_tol);

  if (alerta) {
    if (silenced) {
      // Durante los 5s de silencio, mantenemos el buzzer OFF
      if (now - silenceStart < SILENCE_DURATION) {
        digitalWrite(PIN_BUZZER, HIGH);  // *OFF* 
      } 
      else {
        silenced = false;
        digitalWrite(PIN_BUZZER, LOW);   // *ON*
      }
    }
    else {
      // Alerta normal (no silenciado)
      digitalWrite(PIN_BUZZER, LOW);     // *ON*
    }
  }
  else {
    // Sin alerta, siempre OFF y resetea
    silenced = false;
    digitalWrite(PIN_BUZZER, HIGH);      // *OFF*
  }
}
