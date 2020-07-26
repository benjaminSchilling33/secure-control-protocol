/*
secure-control-protocol
This is the central source file for the secure-control-protocol.

SPDX-License-Identifier: GPL-3.0-or-later

Copyright (C) 2018 Benjamin Schilling
*/

#include "SCP.h"

/**
 * Initialize the SCP library
 */
SCP::SCP()
{
    server = new ESP8266WebServer(80);
    EEPROM.begin(512);
}

void SCP::handleSecureControl()
{
    scpDebug.println(scpDebug.base, "  SCP.handleSecureControl: handleClient");

    String nonce = server->arg("nonce");
    Serial.print("nonce: ");
    Serial.println(nonce);
    String payload = server->arg("payload");
    Serial.print("payload: ");
    Serial.println(payload);
    String payloadLength = server->arg("payloadLength");
    Serial.print("payloadLength: ");
    Serial.println(payloadLength);
    String mac = server->arg("mac");
    Serial.print("mac: ");
    Serial.println(mac);

    // Do input validation
    // check length of nonce
    // check for only valid base64 characters

    // whats the biggest payload? most propably wifi credentials (32 ssid, 32 pw)

    // check length of mac
    // check for only valid base64 characters

    // decrypt payload
    String password = scpPassword.readPassword();
    String decryptedPayload = scpCrypto.decodeAndDecrypt(payload, payloadLength.toInt(), password, nonce, mac);
    Serial.println(decryptedPayload);
    if (decryptedPayload == "")
    {
        sendMalformedPayload();
        return;
    }
    // ====== disassemble payload ======

    // salt
    String salt = decryptedPayload.substring(0, decryptedPayload.indexOf(":"));
    Serial.println("salt: " + salt);
    String remaining = decryptedPayload.substring(decryptedPayload.indexOf(":") + 1);
    Serial.println("remaining after salt: " + remaining);
    // message type
    String messageType = remaining.substring(0, remaining.indexOf(":"));
    Serial.println("messageType: " + messageType);
    remaining = remaining.substring(remaining.indexOf(":") + 1);
    Serial.println("remaining after messageType: " + remaining);
    //device ID
    String deviceId = remaining.substring(0, remaining.indexOf(":"));
    Serial.println("deviceId: " + deviceId);
    remaining = remaining.substring(remaining.indexOf(":") + 1);
    Serial.println("remaining aftet deviceId: " + remaining);

    // if device ID is invalid return
    if (!isDeviceIdValid(deviceId))
    {
        sendMalformedPayload();
        return;
    }
    // depending on message type
    if (messageType == "security-fetch-nvcn")
    {
        scpDebug.println(scpDebug.base, "  SCP.handleSecurityFetchNVCN:  Handled request");
        String answer = scpResponseFactory.createResponseSecurityFetchNVCN(deviceID, scpCrypto.getNVCN());
        server->send(200, "application/json", answer);
        return;
    }
    else
    {
        // Get nvcn
        String nvcn = remaining.substring(0, remaining.indexOf(":"));
        remaining = remaining.substring(remaining.indexOf(":") + 1);
        Serial.println("NVCN: " + nvcn);
        //check NVCN
        if (!isNVCNValid(nvcn))
        {
            sendMalformedPayload();
            return;
        }
        if (messageType == "security-pw-change")
        {
            Serial.println("received security-pw-change");
            remaining = remaining.substring(remaining.indexOf(":") + 1);
            String newPassword = remaining.substring(0, remaining.indexOf(":"));
            scpPassword.writePassword(newPassword);
            String answer = scpResponseFactory.createResponseSecurityPwChange(this->deviceID, String(scpPassword.readCurrentPasswordNumber()), "done");
            answer = scpResponseFactory.createEncryptedResponse(answer);
            server->send(200, "application/json", answer);
            return;
        }
        else if (messageType == "security-wifi-config")
        {
        }
        else if (messageType == "security-reset-to-default")
        {
        }
        else if (messageType == "security-restart")
        {
        }
        else
        {
            sendMalformedPayload();
            return;
        }
    }
    sendMalformedPayload();
}

void SCP::handleDiscoverHello()
{
    scpDebug.println(scpDebug.base, "  SCP.handleDiscoverHello: Message: DiscoverHello");

    String payload = server->arg("payload");

    scpDebug.println(scpDebug.base, "  SCP.handleDiscoverHello:  Payload:" + payload);

    // handle discover-hello message
    if (payload.equals("discover-hello"))
    {
        String currentPasswordNumber = String(scpPassword.readCurrentPasswordNumber());
        String answer = scpResponseFactory.createResponseDiscoverHello(deviceID, deviceType, currentPasswordNumber);
        server->send(200, "application/json", answer);

        scpDebug.println(scpDebug.base, "  SCP.handleDiscoverHello:  discover-response send: " + answer);
    }
    else
    {
        sendMalformedPayload();
    }

    scpDebug.println(scpDebug.base, "  SCP.handleDiscoverHello:  Message End: DiscoverHello");
}

void SCP::sendMalformedPayload()
{
    scpDebug.println(scpDebug.base, "    Error: MalformedPayload");

    String message = "Malformed payload\n\n";
    for (uint8_t i = 0; i < server->args(); i++)
    {
        message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
    }

    server->send(404, "text/plain", message);

    scpDebug.println(scpDebug.base, "    Error End: MalformedPayload");
}

void SCP::handleNotFound()
{
    scpDebug.println(scpDebug.base, "    Error: HandleNotFound");
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server->uri();
    message += "\nMethod: ";
    message += (server->method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server->args();
    message += "\n";
    for (uint8_t i = 0; i < server->args(); i++)
    {
        message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
    }
    server->send(404, "text/plain", message);
    scpDebug.println(scpDebug.base, "    Error End: HandleNotFound");
}

void SCP::handleClient() { server->handleClient(); }

void SCP::provisioningMode()
{
    // Get wifi ssid
    String ssid = this->deviceType + "-" + WiFi.macAddress();
    ssid.replace(":", "");
    String password = "1234567890123456";

    // Set Wifi persistent to false,
    // otherwise on every WiFi.begin the
    // ssid and password will be written to
    // the same area in the flash which is
    // destroy the device in the long run
    // See: https://github.com/esp8266/Arduino/issues/1054
    WiFi.persistent(false);
    WiFi.softAP(ssid, password);
    scpDebug.print(scpDebug.base, "  SCP.provisioningMode: Open Wifi: ");
    scpDebug.println(scpDebug.base, ssid);
    scpDebug.print(scpDebug.base, "  SCP.provisioningMode: Password: ");
    scpDebug.println(scpDebug.base, password);
}

void SCP::controlMode()
{
    String wifiSSID = scpEepromController.getWifiSSID();
    String wifiPassword = scpEepromController.getWifiPassword();

    // Set Wifi persistent to false,
    // otherwise on every WiFi.begin the
    // ssid and password will be written to
    // the same area in the flash which is
    // destroy the device in the long run
    // See: https://github.com/esp8266/Arduino/issues/1054
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);

    // Wait until connection is established
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        digitalWrite(D5, !digitalRead(D5));
    }

    scpDebug.print(scpDebug.base, "  SCP.controlMode: Connected to Wifi:");
    scpDebug.println(scpDebug.base, wifiSSID);
    scpDebug.print(scpDebug.base, "  SCP.controlMode: IP address:");
    String ipAddress = WiFi.localIP().toString();
    scpDebug.println(scpDebug.base, ipAddress);
}

void SCP::init(String deviceType)
{
    // Set the device type
    this->deviceType = deviceType;

    // If the default password was not set, set it now
    if (!scpPassword.isDefaultPasswordSetOnce())
    {
        scpDebug.println(scpDebug.base, "  SCP.init: password not set, initializing default password");
        scpPassword.setDefaultPassword();
    }

    // If the device id is not set, set it now
    if (!scpDeviceID.isDeviceIDSet())
    {
        scpDebug.println(scpDebug.base, "  SCP.init: device ID not set, setting device ID");
        scpDeviceID.setDeviceID();
    }

    // read the device id
    deviceID = scpDeviceID.readDeviceID();

    scpDebug.println(scpDebug.base, "  SCP.init: DeviceID: " + deviceID);

    // if the default password is set or no wifi credentials are set,
    // go to provisioning mode, otherwise go to control mode
    if (scpPassword.readPassword() == scpPassword.DEFAULT_PW && !scpEepromController.areWifiCredentialsSet())
    {
        scpDebug.println(scpDebug.base, "  SCP.init: Default password set and no Wifi Credentials available, going to provisioning mode.");
        provisioningMode();
    }
    else if (scpPassword.readPassword() != scpPassword.DEFAULT_PW && !scpEepromController.areWifiCredentialsSet())
    {
        scpDebug.println(scpDebug.base, "  SCP.init: New password set but no Wifi Credentials available, going to provisioning mode.");
        scpPassword.setDefaultPassword();
        provisioningMode();
    }
    else
    {
        controlMode();
    }

    server->on("/secure-control", std::bind(&SCP::handleSecureControl, this));
    server->on("/secure-control/discover-hello",
               std::bind(&SCP::handleDiscoverHello, this));
    server->onNotFound(std::bind(&SCP::handleNotFound, this));
    server->begin();

    scpDebug.println(scpDebug.base, "  SCP.init: HTTP server started");

    scpDebug.println(scpDebug.base, "  SCP.init: SCP initialized");
}

void SCP::registerControlUpFunction(std::function<void()> fun)
{
    controlUpFunction = fun;
}

void SCP::registerControlDownFunction(std::function<void()> fun)
{
    controlDownFunction = fun;
}

void SCP::registerControlStopFunction(std::function<void()> fun)
{
    controlStopFunction = fun;
}

bool SCP::isDeviceIdValid(String devId)
{
    if (devId.equals(this->deviceID))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool SCP::isNVCNValid(String nvcn)
{
    if (scpCrypto.checkNVCN(nvcn))
    {
        return true;
    }
    else
    {
        return false;
    }
}