/* WiFi Example
 * Copyright (c) 2018 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "TCPSocket.h"
#include "HTS221Sensor.h"
#include "LPS22HBSensor.h"
#include "LSM6DSLSensor.h"

#define WIFI_IDW0XX1    2

#if (defined(TARGET_DISCO_L475VG_IOT01A) || defined(TARGET_DISCO_F413ZH))
#include "ISM43362Interface.h"
ISM43362Interface wifi(MBED_CONF_APP_WIFI_SPI_MOSI, MBED_CONF_APP_WIFI_SPI_MISO, MBED_CONF_APP_WIFI_SPI_SCLK, MBED_CONF_APP_WIFI_SPI_NSS, MBED_CONF_APP_WIFI_RESET, MBED_CONF_APP_WIFI_DATAREADY, MBED_CONF_APP_WIFI_WAKEUP, false);

#else // External WiFi modules

#if MBED_CONF_APP_WIFI_SHIELD == WIFI_IDW0XX1
#include "SpwfSAInterface.h"
SpwfSAInterface wifi(MBED_CONF_APP_WIFI_TX, MBED_CONF_APP_WIFI_RX);
#endif // MBED_CONF_APP_WIFI_SHIELD == WIFI_IDW0XX1

#endif

#ifdef TARGET_DISCO_L475VG_IOT01A

#include "lis3mdl_class.h"
#include "VL53L0X.h"

#else // Nucleo-XXX + X-Nucleo-IKS01A2 or SensorTile

#ifdef TARGET_NUCLEO_L476RG
#define TARGET_SENSOR_TILE   // comment out to use actual NUCLEO-L476RG instead of SensorTile
#endif

#include "LSM303AGRMagSensor.h"
#include "LSM303AGRAccSensor.h"

#endif

/* Retrieve the composing elements of the expansion board */

/* Interface definition */
#ifdef TARGET_DISCO_L475VG_IOT01A
static DevI2C devI2c(PB_11,PB_10);
#else // X-Nucleo-IKS01A2 or SensorTile
#ifdef TARGET_SENSOR_TILE
#define SPI_TYPE_LPS22HB   LPS22HBSensor::SPI3W
#define SPI_TYPE_LSM6DSL   LSM6DSLSensor::SPI3W
SPI devSPI(PB_15, NC, PB_13);  // 3-wires SPI on SensorTile  
static Serial ser(PC_12,PD_2); // Serial with SensorTile Cradle Exp. Board + Nucleo   
#define printf(...) ser.printf(__VA_ARGS__)     
#else  // Nucleo-XXX + X-Nucleo-IKS01A2 
static DevI2C devI2c(D14,D15);
#endif
#endif

/* Environmental sensors */
#ifdef TARGET_SENSOR_TILE
static LPS22HBSensor press_temp(&devSPI, PA_3, NC, SPI_TYPE_LPS22HB); 
#else  // Nucleo-XXX + X-Nucleo-IKS01A2 or B-L475E-IOT01A2
static LPS22HBSensor press_temp(&devI2c);
static HTS221Sensor hum_temp(&devI2c);
#endif

/* Motion sensors */
#ifdef TARGET_DISCO_L475VG_IOT01A
static LSM6DSLSensor acc_gyro(&devI2c,LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW,PD_11); // low address
static LIS3MDL magnetometer(&devI2c);
#else // X-NUCLEO-IKS01A2 or SensorTile
#if defined (TARGET_SENSOR_TILE)
static LSM6DSLSensor acc_gyro(&devSPI,PB_12, NC, PA_2, SPI_TYPE_LSM6DSL); 
static LSM303AGRMagSensor magnetometer(&devSPI, PB_1);
static LSM303AGRAccSensor accelerometer(&devSPI, PC_4);
#else
static LSM6DSLSensor acc_gyro(&devI2c,LSM6DSL_ACC_GYRO_I2C_ADDRESS_HIGH,D4,D5); // high address
static LSM303AGRMagSensor magnetometer(&devI2c);
static LSM303AGRAccSensor accelerometer(&devI2c);
#endif
#endif

/* Range sensor - B-L475E-IOT01A2 only */
#ifdef TARGET_DISCO_L475VG_IOT01A
static DigitalOut shutdown_pin(PC_6);
static VL53L0X range(&devI2c, &shutdown_pin, PC_7);
#endif

/* Wifi Acces Point Settings */ 
#define AP_SSID         "XXXXXXXXXXX"            
#define AP_PASSWORD     "XXXXXXXXXXX"
#define UBIDOTS_SERVER  "things.ubidots.com"
#define UBIDOTS_PORT    80
#define UBIDOTS_TOKEN   "XXXXXXXXXXX"
#define UBIDOTS_DEVICE  "XXXXXXXXXXX"

/* Private defines -----------------------------------------------------------*/
#define WIFI_WRITE_TIMEOUT 10000
#define WIFI_READ_TIMEOUT  10000
#define CONNECTION_TRIAL_MAX          10

/* Private typedef------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
Serial pc(SERIAL_TX, SERIAL_RX);
char* modulename;
uint8_t  MAC_Addr[6]; 
uint8_t  IP_Addr[4]; 
bool level = false;
uint8_t RemoteIP[] = {MBED_CONF_APP_SERVER_IP_1,MBED_CONF_APP_SERVER_IP_2,MBED_CONF_APP_SERVER_IP_3, MBED_CONF_APP_SERVER_IP_4};
uint8_t RxData [1024];
uint16_t respLen;

/* Digital ressources */
DigitalOut myLed(LED1);



const char *sec2str(nsapi_security_t sec)
{
    switch (sec) {
        case NSAPI_SECURITY_NONE:
            return "None";
        case NSAPI_SECURITY_WEP:
            return "WEP";
        case NSAPI_SECURITY_WPA:
            return "WPA";
        case NSAPI_SECURITY_WPA2:
            return "WPA2";
        case NSAPI_SECURITY_WPA_WPA2:
            return "WPA/WPA2";
        case NSAPI_SECURITY_UNKNOWN:
        default:
            return "Unknown";
    }
}

int scan_demo(WiFiInterface *wifi)
{
    WiFiAccessPoint *ap;

    printf("Scan:\n");

    int count = wifi->scan(NULL,0);
    printf("%d networks available.\n", count);

    /* Limit number of network arbitrary to 15 */
    count = count < 15 ? count : 15;

    ap = new WiFiAccessPoint[count];
    count = wifi->scan(ap, count);
    for (int i = 0; i < count; i++)
    {
        printf("Network: %s secured: %s BSSID: %hhX:%hhX:%hhX:%hhx:%hhx:%hhx RSSI: %hhd Ch: %hhd\n", ap[i].get_ssid(),
               sec2str(ap[i].get_security()), ap[i].get_bssid()[0], ap[i].get_bssid()[1], ap[i].get_bssid()[2],
               ap[i].get_bssid()[3], ap[i].get_bssid()[4], ap[i].get_bssid()[5], ap[i].get_rssi(), ap[i].get_channel());
    }

    delete[] ap;
    return count;
}

void http_demo(NetworkInterface *net)
{
    TCPSocket socket;
    nsapi_error_t response;

    printf("Sending HTTP request to www.arm.com...\n");

    // Open a socket on the network interface, and create a TCP connection to www.arm.com
    socket.open(net);
    response = socket.connect("www.arm.com", 80);
    if(0 != response) {
        printf("Error connecting: %d\n", response);
        socket.close();
        return;
    }

    // Send a simple http request
    char sbuffer[] = "GET / HTTP/1.1\r\nHost: www.arm.com\r\n\r\n";
    nsapi_size_t size = strlen(sbuffer);
    response = 0;
    while(size)
    {
        response = socket.send(sbuffer+response, size);
        if (response < 0) {
            printf("Error sending data: %d\n", response);
            socket.close();
            return;
        } else {
            size -= response;
            // Check if entire message was sent or not
            printf("sent %d [%.*s]\n", response, strstr(sbuffer, "\r\n")-sbuffer, sbuffer);
        }
    }

    // Recieve a simple http response and print out the response line
    char rbuffer[64];
    response = socket.recv(rbuffer, sizeof rbuffer);
    if (response < 0) {
        printf("Error receiving data: %d\n", response);
    } else {
        printf("recv %d [%.*s]\n", response, strstr(rbuffer, "\r\n")-rbuffer, rbuffer);
    }

    // Close the socket to return its memory and bring down the network interface
    socket.close();
}


int sendUbidotsData(NetworkInterface *net) {
    TCPSocket socket;
    nsapi_error_t response;

    char sendMyBuffer[1024];
    char message[64];
    int err;
    float value1, value2, value3;

    printf("Connecting to Ubidots...\r\n");


    // Open a socket on the network interface, and create a TCP connection to www.arm.com
    socket.open(net);
    err = socket.connect(UBIDOTS_SERVER, UBIDOTS_PORT); 
    if(0 != err) {
        printf("Error connecting: %d\n", err);
        socket.close();
        return -1;
    }
    else 
        printf("\r\nconnected to host server\r\n"); 

    /* Construct content of HTTP command */
    #ifndef TARGET_SENSOR_TILE    
    hum_temp.get_temperature(&value1);
    hum_temp.get_humidity(&value2);
    printf("HTS221:  [temp] %.2f C, [hum]   %.2f%%\r\n", value1, value2);
    #endif    
    value1=0;    
    press_temp.get_temperature(&value1);
    press_temp.get_pressure(&value3);
    printf("LPS22HB: [temp] %.2f C, [press] %.2f mbar\r\n", value1, value3);
                            
    sprintf(message, "{\"temperature\": %0.2f, \"humidity\": %0.2f, \"pressure\": %0.2f, \"level\": %d}", value1, value2, value3, (int)level);
    printf("Content Length = %d\r\n", (int)strlen(message));
        
    /* Construct HTTP command to send */
    sprintf(sendMyBuffer, "POST /api/v1.6/devices/%s/?token=%s HTTP/1.1\r\nHost: things.ubidots.com\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", UBIDOTS_DEVICE, UBIDOTS_TOKEN, (int)strlen(message),message); 
    printf("HTTP command %s\r\n", sendMyBuffer);
     
    /* Send http request to Ubidots */ 
    int scount = socket.send(sendMyBuffer, (int)strlen(sendMyBuffer));
    printf("sent %d [%.*s]\r\n", scount, strstr(sendMyBuffer, "\r\n") - sendMyBuffer, sendMyBuffer);

    /* Receive a simple http response and print out the response line */
    char respBuffer[64];
    int rcount = socket.recv(respBuffer, sizeof respBuffer);
    if (rcount < 0) {
        printf("Error receiving data: %d\n", response);
    } else {
        printf("recv %d [%.*s]\r\n", rcount, strstr(respBuffer, "\r\n") - respBuffer, respBuffer);
    }

    /* Close the socket to return its memory and bring down the network interface */
    printf("Close Socket\r\n");
    socket.close();
    return 0;
}

int main()
{
    int retUbidots;
    int count = 0;
    uint8_t id;    
    pc.baud(115200);

 
    /* Init all sensors with default params */
    #ifndef TARGET_SENSOR_TILE
        hum_temp.init(NULL);
    #endif  
    
        press_temp.init(NULL);
    
      /* Enable all sensors */
    #ifndef TARGET_SENSOR_TILE  
        hum_temp.enable();
    #endif  
        press_temp.enable();
    
    #ifndef TARGET_SENSOR_TILE
        hum_temp.read_id(&id);
    #endif  
        press_temp.read_id(&id);
                    
        printf("\n");
        printf("************************************************************\n");
        printf("***   STM32 IoT Discovery kit for STM32L475 MCU          ***\n");
        printf("***   Litteton Community Farm Environment Sensor         ***\n");
        printf("***        by Renato M. Nakagomi - May 2018              ***\n");
        printf("************************************************************\n");
    
  while(1) {
    count = scan_demo(&wifi);
    if (count == 0) {
        printf("No WIFI APNs found - can't continue further.\n");
        return -1;
    }

    printf("\nConnecting to %s...\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi.connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
        printf("\nConnection error\n");
        return -1;
    }

    printf("Success\n\n");
    printf("MAC: %s\n", wifi.get_mac_address());
    printf("IP: %s\n", wifi.get_ip_address());
    printf("Netmask: %s\n", wifi.get_netmask());
    printf("Gateway: %s\n", wifi.get_gateway());
    printf("RSSI: %d\n\n", wifi.get_rssi());

//    http_demo(&wifi);

    retUbidots = 0;
    while (retUbidots == 0) {
        retUbidots = sendUbidotsData(&wifi);
        wait(5.0);
    }
    wifi.disconnect();
    printf("\nDone\n");
  } // while(1)
}
