// telegram.cpp
#include <WiFiClientSecure.h>

const char* telegramToken = "TU_TELEGRAM_BOT_TOKEN";
const char* chatID = "TU_TELEGRAM_CHAT_ID";

void enviarAlertaTelegram(String mensaje) {
    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect("api.telegram.org", 443)) {
        Serial.println("Error conectando a Telegram");
        return;
    }

    String url = "/bot" + String(telegramToken) + "/sendMessage";
    String body = "chat_id=" + String(chatID) + "&text=" + mensaje;

    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println("Content-Length: " + String(body.length()));
    client.println();
    client.println(body);

    delay(1000);
    Serial.println("Mensaje enviado a Telegram");
    client.stop();
}
