#include <Arduino.h> //viene por defecto
#include "IoTicosSplitter.h" //copiado de ioticos
#include "RestClient.h" //Libreria Robert cuthbertson
#include <HTTPClient.h>
#include "Colors.h"
#include <SPI.h>
#include <Ethernet.h> //Libreria EtherCard by Paul stoffregen
#include <ArduinoJson.h> //libreria ArduinoJson by Benoit Blanchon
#include <DHT_U.h> //libreria DHT_Unified_sensor by Adafruit
#include <PubSubClient.h> //Libreria Pubsubclient by Nick O´leary
#include <LiquidCrystal_I2C.h> //Libreria LiquidCrystal_I2C by Frank Brabander

//video 194 variables a setear
String dId = "12341234"; //esta yo se la defino a mi arduino para pejelagartero
String webhook_pass = "VFduACaDLs"; //esta me la da la aplicacion
String webhook_endpoint = "http://192.168.1.76:3001/api/getdevicecredentials"; //endpoint donde pedira las credenciales
const char *mqtt_server = "192.168.1.76"; //192.168.1.76 para la mac

//configuracion de ethernet shield
// Dirección física del Arduino
byte ethernet_mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xED, 0xAC }; //esta direccion puede cambiar en este caso sera para peje
IPAddress ip(192,168,1,201); //201 para pejelagartero, 200 para iglpmct, 20 para iglptti aun que podrian darmelo RCDT 
//en la red del trabajo me asigno la 13.4.30.57
IPAddress gateway(192,168,1,254);   //13.4.30.1  //hogar 192.168.1.254
IPAddress subnet(255,255,255,0); 

//PINS
#define led 2 //pin indicador de alarma BUILTIN_LED

// //WiFi solo para el esp o conexiones via wifi
// const char *wifi_ssid = "INFINITUM72D1_2.4"; //INFINITUM72D1_2.4 - INFINITUM59W1_2.4
// const char *wifi_password = "5X4X3EeaX9"; //5X4X3EeaX9 - unJvpTX5Vp

// configuracion del DHT22

float temp; // Variable para almacenar el valor obtenido del sensor 
const int DHTPin = A0; // Variable del pin de entrada del sensor (A0)
DHT dht(DHTPin, DHT22);

//pines de alarma
const int cantidadAlarmas = 12;
int entradaAlarmas[cantidadAlarmas] = {22,24,26,28,30,32,34,36,38,40,42,44}; //Allocate 10 spaces and name the output pin address.
int indicadoresLed[cantidadAlarmas] = {23,25,27,29,31,33,35,37,39,41,43,45};

//configuracion de reinicio arduino
#define Reinicio asm("jmp 0x0000") //para REINICIAR ARDUINO

//configuracion para cristal i2c con adaptador serial
LiquidCrystal_I2C lcd(0x27,16,2);  // SDA - A4 ,SCL - A5
//mas info https://naylampmechatronics.com/blog/35_tutorial-lcd-con-i2c-controla-un-lcd-con-solo-dos-pines.html

//pines a usar en el arduino 
//#define led 2 //pin donde esta el pin

//definicion de funciones
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void process_actuators();
void send_data_to_broker();
void callback(char *topic, byte *payload, unsigned int length);
void process_incoming_msg(String topic, String incoming);
void sinConexion(); //aun por escribir
void sinConexion2(); //aun por escribir
void clear(); //solo funciona para el esp32
void restart();
void print_stats();



//mas variables globales

EthernetClient espclient;
PubSubClient client(espclient);
IoTicosSplitter splitter;
long lastReconnectAttemp = 0;
long varsLastSend[cantidadAlarmas+1]; //variable que indica la cantidad de topicos
String last_received_msg = "";
String last_received_topic = "";
int prev_temp = 0;
int enviar = 0; //indica que hay que inviar informacion al broker
int cont=0; //cuenta cuantas veces a leido la lectura de la temperatura
DynamicJsonDocument mqtt_data_doc(2048);

//inicio setup
void setup()
{
  dht.begin();
  lcd.init();
  lcd.backlight();
  Serial.begin(9600);
  pinMode(led, OUTPUT); //indica si existe alguna alarma
  //clear(); //solo funciona para el esp32
  
  //configuracion de pines de salida donde iran los leds (impares)
  for (int var = 0; var < cantidadAlarmas; var++){
    pinMode(indicadoresLed[var], OUTPUT); 
    digitalWrite(indicadoresLed[var],LOW);
  }
  //configuracion de los pines de entrada son los pines pares
  for (int var = 0; var < cantidadAlarmas; var++){
    pinMode(entradaAlarmas[var], INPUT_PULLUP); 
    Serial.print(entradaAlarmas[var]);
    digitalWrite(entradaAlarmas[var],HIGH);
  }

  Serial.print("\n\n\nEthernet Connection in progres");
  
  int counter = 0; // intentos de conexion
  Ethernet.begin(ethernet_mac, ip, gateway, subnet); //https://programarfacil.com/blog/arduino-blog/ethernet-shield-arduino/
  while(Ethernet.begin(ethernet_mac)<= 0) //https://programarfacil.com/blog/arduino-blog/ethernet-shield-arduino/) 
  {
    delay(500);
    Serial.print(".");
    counter++;

    if (counter > 10)
    { 
      counter = 0;
      Serial.print("  ⤵");
      Serial.print("\n\n         Verificar el cable de Ethernet :( ");
      delay(5000);
      sinConexion(); //funcion que ejecute la medicion de temperatura e indicar que esta a la espera de la conexion ethernet 
    }
  }

  Serial.print("  ⤵");

  //Printing local ip
  Serial.println("\n\n         Ethernet Connection -> SUCCESS :)");
  Serial.print("\n         Local IP -> ");
  Serial.print(Ethernet.localIP());// mandamos a imprimir la direccion ip
  Serial.print("\n         Local Gateway -> ");
  Serial.print(Ethernet.gatewayIP());
  Serial.print("\n         Local Mask -> ");
  Serial.print(Ethernet.subnetMask());
  Serial.println("");
  client.setCallback(callback); //se activa el callback con la funcion clien.loop

}

void loop()
{
    check_mqtt_connection();
    
}



//USER FUNTIONS ⤵ Sensor de Temperatura y sensores
void process_sensors()
{
  //variable local que cuenta cuantas veces se indica el valor de la temperatura
  int valor = 1;
  temp=dht.readTemperature();
  //Serial.println(temp);
  for (int var = 0; var < cantidadAlarmas+1; var++){
    if(var==0){
      mqtt_data_doc["variables"][0]["last"]["value"] = temp;
      
      //save temp?
      if(cont > 500){ //cada 500 lecturas salvara la medicion
      cont = 0;
      
      }
      else
      {
        cont ++;
        mqtt_data_doc["variables"][0]["last"]["save"] = 0;
        enviar=0; 
        if(cont==100){
          mqtt_data_doc["variables"][0]["last"]["save"] = 1;
          Serial.println("Manda a salvar el dato");
          enviar=1;
        }
        if(cont==200 && temp > 29){
          mqtt_data_doc["variables"][0]["last"]["save"] = 1;
          Serial.println("Manda a salvar el dato");
          enviar=1;
        }
      }
  //inicia las variables programadas
    }else{
      if(digitalRead(entradaAlarmas[var-1])== true){
        valor = 0;
      }else{
        valor = 1;
      }
    mqtt_data_doc["variables"][var]["last"]["value"]=valor;
    digitalWrite(indicadoresLed[var-1],valor); //para poner leds
    //Serial.print(digitalRead(entradaAlarmas[var]));
    }
  }
  
}

//actuadores que manda a encender o apagar cualquier cosa aun sin uso
void process_actuators()
{
  if (mqtt_data_doc["variables"][9]["last"]["value"] == "Encender")
  {
    digitalWrite(led, HIGH);
    mqtt_data_doc["variables"][9]["last"]["value"] = "";
    varsLastSend[4] = 0;
  }
  else if (mqtt_data_doc["variables"][10]["last"]["value"] == "Apagar")
  {
    digitalWrite(led, LOW);
    mqtt_data_doc["variables"][10]["last"]["value"] = "";
    varsLastSend[4] = 0;
  }

}




//TEMPLATE ⤵
void process_incoming_msg(String topic, String incoming){

  last_received_topic = topic;
  last_received_msg = incoming;

  String variable = splitter.split(topic, '/', 2);

  for (int i = 0; i < int(mqtt_data_doc["variables"].size()); i++ ){

    if (mqtt_data_doc["variables"][i]["variable"] == variable){
      
      DynamicJsonDocument doc(256);
      deserializeJson(doc, incoming);
      mqtt_data_doc["variables"][i]["last"] = doc;

      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;

    }

  }

  //process_actuators(); //lo dejo pero no mando a actuar a nada

}
//cuando se recibe algo
void callback(char *topic, byte *payload, unsigned int length)
{

  String incoming = "";

  for (int i = 0; i < int(length); i++)
  {
    incoming += (char)payload[i];
  }

  incoming.trim();

  process_incoming_msg(String(topic), incoming);

}
//se envian datos al broker
void send_data_to_broker()
{

  long now = millis();
  for (int i = 0; i < cantidadAlarmas+1; i++) //catidadAlarmas+1 por que se agrega el sensor de temperatura
  {
    
    if (mqtt_data_doc["variables"][i]["variableType"] == "output")
    {
      continue;
    }

    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];
    
    if (now - varsLastSend[i] > freq * 1000 || enviar == 1) // envia info cada 1 seg
    {
      varsLastSend[i] = millis();
      enviar=0; //para reiniciar ya que si no se queda con valor a uno y tarda mas de 2 min en mandar datos
      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());
      
      // Serial.print(topic.c_str());
      // Serial.println(toSend.c_str()); //dato a enviar


      //STATS
      // long counter = mqtt_data_doc["variables"][i]["counter"];
      // counter++;
      // mqtt_data_doc["variables"][i]["counter"] = counter;
      

    }
  }
}

bool reconnect()
{

  if (!get_mqtt_credentials())
  {
    Serial.println("\n\n      Error getting mqtt credentials :( \n\n RESTARTING IN 10 SECONDS");
    sinConexion2();
    delay(10000);
    restart();
  }

  //Setting up Mqtt Server
  client.setServer(mqtt_server, 1883);

  Serial.println("\n\n\nIntentando Conexion MQTT  ⤵");
  Serial.println("Linea 276");
  String str_client_id = "device_" + dId + "_" + random(1, 9999);
  const char *username = mqtt_data_doc["username"];
  const char *password = mqtt_data_doc["password"];
  String str_topic = mqtt_data_doc["topic"];

  if (client.connect(str_client_id.c_str(), username, password))
  {
    Serial.println("\n\n         Mqtt Client Connected :) ");
    delay(2000);

    client.subscribe((str_topic + "+/actdata").c_str());
    return true;
  }
  else
  {
    Serial.print("\n\n         Mqtt Client Connection Failed :( ");
  }
  return false;
}

void check_mqtt_connection()
{

  if (Ethernet.begin(ethernet_mac)<=0) //si esto es menor igual a cero no hay conexion Ethernet
  {
    Serial.print("\n\n         Desconexion Cable Ethernet :( ");
    Serial.println(" -> Restarting...");
    sinConexion2();
    delay(10000);
    restart(); //aqui si tengo que reiniciar el arduino
  }

  if (!client.connected()) //a emqtt
  {

    long now = millis();

    if (now - lastReconnectAttemp > 5000)
    {
      lastReconnectAttemp = millis();
      if (reconnect())
      {
        lastReconnectAttemp = 0;
      }
    }
  }
  else
  {
    client.loop();
    process_sensors();
    send_data_to_broker();
    print_stats();
  }
}


bool get_mqtt_credentials()
{
  
  Serial.println(underlinePurple + "\n\n\nGetting MQTT Credentials from WebHook    ⤵");
  delay(1000);

  String toSend = "dId=" + dId + "&password=" + webhook_pass;

  HttpClient http = HttpClient(espclient, mqtt_server, 3001);
  if(http.connect(mqtt_server, 3001)) {
  Serial.println("Conectado al servidor");
  http.println("POST /api/getdevicecredentials HTTP/1.1");
  http.print("Host:");
  http.println(mqtt_server);
  http.println("Connection: keep-alive");
  http.println("Content-Type: application/x-www-form-urlencoded");
  http.print("Content-Length: ");
  http.println(toSend.length());
  http.println("");
  http.println(toSend);
  } else {
  Serial.println("Fallo la conexion al servidor linea 353");
  }

if(!http.connected())
{
  // if the server's disconnected, stop the client:
  Serial.println("Desconectado");
  http.flush();
  http.stop();
}

  int response = http.responseStatusCode();

  if (response < 0)
  {
    Serial.println("\n\n         Error Sending Post Request :( Error con la libreria");
    http.flush();
    http.stop();
    return false;
  }

  if (response != 200)
  {
    Serial.print("\n\n         Error in response :(   e-> " + response);
    http.flush();
    http.stop();
    return false;
  }

  if (response == 200)
  {
    String responseBody = http.responseBody();  //http.readString()
    //Serial.println(responseBody);
    Serial.print("\n\n         Mqtt Credentials Obtained Successfully :) linea 412");
    deserializeJson(mqtt_data_doc, responseBody); //pendiente la conexion a mqtt
    http.flush();
    http.stop();
    delay(1000);
  }

  return true;
}

void restart(){
  Reinicio;
}

//limpia la pantalla de la terminal solo para el esp32
// void clear()
// {
//   Serial.write(27);    // ESC command
//   Serial.print("[2J"); // clear screen command
//   Serial.write(27);
//   Serial.print("[H"); // cursor to home command
// }

long lastStats = 0;

void sinConexion(){
  if (millis() % 5000==0){
  temp=dht.readTemperature(); 
  lcd.setCursor(0,0);
  lcd.print("SIN RED");
  lcd.setCursor(0,1);
  lcd.print("Temp:");
  lcd.print(3,1); //1 decimal
  lcd.print(" C");
  }
}
void sinConexion2(){ 
  lcd.setCursor(0,0);
  lcd.print(" Falla conexion ");
  lcd.setCursor(0,1);
  lcd.print("Reiniciando.....");
}
//esto que da para posible despues--------------------------------
void print_stats()
{
    
    // clear(); //solo funciona para el esp32
    Serial.print("-----------------------------------");
    Serial.print(cont);
    Serial.println("---------------------------------");
    Serial.print("# \t Name \t\t\t Var \t\t Type \t\t Msg\n\n");

    for (int i = 0; i < cantidadAlarmas+1; i++)
    {

      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      // long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + " \t " + variableFullName.substring(0,12) + " \t\t " + variable.substring(0,10) + " \t " + variableType.substring(0,5) + " \t " + lastMsg);
    }
  
}