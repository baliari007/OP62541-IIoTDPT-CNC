#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <tlhelp32.h>

#define MAX_PROCESSORS 64

static UA_Float currentTemperature = 30.0; // Initial dummy temperature value
static UA_Boolean highTempAlarm = false; // High temperature alarm variable

void readArduinoData(float *temperature, float *humidity, float *accelX, float *accelY, float *accelZ) {
    HANDLE hSerial;
    DCB dcbSerialParams = {0};
    COMMTIMEOUTS timeouts = {0};
    static char buffer[256];
    static int bufferIndex = 0;
    DWORD bytesRead;

    // Open the serial port
    hSerial = CreateFile("COM4", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("Error opening serial port\n");
        return;
    }

    // Set device parameters (9600 baud, 1 start bit, 1 stop bit, no parity)
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    GetCommState(hSerial, &dcbSerialParams);
    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    SetCommState(hSerial, &dcbSerialParams);

    // Set COM port timeout settings
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    // Read data from the serial port
    if (ReadFile(hSerial, buffer + bufferIndex, sizeof(buffer) - bufferIndex - 1, &bytesRead, NULL)) {
        bufferIndex += bytesRead;
        buffer[bufferIndex] = '\0'; // Null-terminate the string

        // Check for complete JSON object
        char *start = strchr(buffer, '{');
        char *end = strchr(buffer, '}');
        if (start && end && end > start) {
            *end = '\0'; // Null-terminate the JSON string
            printf("Received data: %s\n", start); // Print the received data

            // Parse the JSON data
            int parsed = sscanf(start, "{\"temperature\":%f,\"humidity\":%f,\"accelX\":%f,\"accelY\":%f,\"accelZ\":%f}", temperature, humidity, accelX, accelY, accelZ);
            if (parsed == 5) {
                printf("Parsed data - temperature: %f, humidity: %f, accelX: %f, accelY: %f, accelZ: %f\n", *temperature, *humidity, *accelX, *accelY, *accelZ);
            } else {
                printf("Error parsing data\n");
            }

            // Remove the processed JSON object from the buffer
            bufferIndex = 0;
        } else if (bufferIndex >= sizeof(buffer) - 1) {
            // Buffer overflow, reset buffer
            bufferIndex = 0;
            printf("Buffer overflow, resetting buffer\n");
        }
    } else {
        printf("Error reading from serial port\n");
    }

    // Close the serial port
    CloseHandle(hSerial);
}

static void updateValuesCallback(UA_Server *server, void *data) {
    // Read data from Arduino
    float temperature = 0.0, humidity = 0.0, accelX = 0.0, accelY = 0.0, accelZ = 0.0;
    readArduinoData(&temperature, &humidity, &accelX, &accelY, &accelZ);

    // Print the values read from Arduino
    printf("temperature: %f, humidity: %f, accelX: %f, accelY: %f, accelZ: %f\n", temperature, humidity, accelX, accelY, accelZ);

    // Send Arduino data to OPC UA server
    UA_Variant tempValue, humidityValue, accelXValue, accelYValue, accelZValue;
    UA_Variant_setScalar(&tempValue, &temperature, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, "temperature"), tempValue);
    UA_Variant_setScalar(&humidityValue, &humidity, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, "humidity"), humidityValue);
    UA_Variant_setScalar(&accelXValue, &accelX, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, "accelerationX"), accelXValue);
    UA_Variant_setScalar(&accelYValue, &accelY, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, "accelerationY"), accelYValue);
    UA_Variant_setScalar(&accelZValue, &accelZ, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, "accelerationZ"), accelZValue);
}

static void addArduinoVariable(UA_Server *server, const char *nodeName, UA_Float *value) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, value, &UA_TYPES[UA_TYPES_FLOAT]);
    attr.displayName = UA_LOCALIZEDTEXT("en-US", (char *)nodeName);
    UA_NodeId nodeId = UA_NODEID_STRING(1, (char *)nodeName);
    UA_QualifiedName name = UA_QUALIFIEDNAME(1, (char *)nodeName);
    UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_Server_addVariableNode(server, nodeId, parentNodeId, parentReferenceNodeId, name, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, NULL, NULL);
}

int main(void) {
    srand(time(NULL)); // Seed the random number generator
    UA_Boolean running = true;
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(config, 4840, NULL); // Set the port to 4840 or your desired port

    // Set the server URL
    UA_String serverUrl = UA_STRING("opc.tcp://192.168.0.13:4840");
    config->serverUrls = (UA_String *)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]); // Allocate memory for serverUrls
    config->serverUrlsSize = 1; // Set the size of serverUrls array
    UA_String_copy(&serverUrl, &config->serverUrls[0]); // Copy the serverUrl to the serverUrls array

    // Add Arduino data nodes
    float temperature = 0.0, humidity = 0.0, accelX = 0.0, accelY = 0.0, accelZ = 0.0;
    addArduinoVariable(server, "temperature", &temperature);
    addArduinoVariable(server, "humidity", &humidity);
    addArduinoVariable(server, "accelerationX", &accelX);
    addArduinoVariable(server, "accelerationY", &accelY);
    addArduinoVariable(server, "accelerationZ", &accelZ);

    // Add repeated callback to update the temperature, humidity, and accelerometer values every second
    UA_Server_addRepeatedCallback(server, updateValuesCallback, NULL, 1000, NULL);

    UA_StatusCode status = UA_Server_run(server, &running);
    UA_Server_delete(server);
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}