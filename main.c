#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <postgresql/libpq-fe.h>
#include <ctype.h>
#include <jansson.h>

#define PORT 8080

void handle_options(int newSocket) {
    const char *response = 
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "\r\n";
    send(newSocket, response, strlen(response), 0);
}

void handle_login(int newSocket, char *buffer, PGconn *conn) {
    printf("Entrato in handle_login\n");
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        printf("Entrato in body\n");
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email e la password
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *password = json_string_value(json_object_get(json_body, "password"));
        if (!email || !password) {
            printf("Email o password mancanti nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);
        printf("Password ricevuta: %s\n", password);

        // Esegui la query al database e invia la risposta al client
        char query[512];
        snprintf(query, sizeof(query), "SELECT * FROM venditore WHERE indirizzo_email='%s' AND password='%s'", email, password);

        // Stampa la query con i valori correnti
        printf("Query eseguita: %s\n", query);

        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            printf("Utente trovato\n");

            // Costruisci la risposta HTTP
            const char *response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "utente trovato";
            send(newSocket, response, strlen(response), 0);
        } else {
            printf("Utente non trovato\n");

            // Costruisci la risposta HTTP
            const char *response = 
                "HTTP/1.1 401 Unauthorized\r\n"
                "Content-Type: text/plain\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "Credenziali non valide";
            send(newSocket, response, strlen(response), 0);
        }

        PQclear(res);
        json_decref(json_body);
    }
}
void handle_get_auctions(int newSocket, PGconn *conn) {
    // Recupero delle aste dal database e invio al client
    PGresult *res = PQexec(conn, "SELECT id, nome, id_venditore, intervalloTempoOfferte FROM asta_allinglese ORDER BY id DESC LIMIT 2");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
        PQclear(res);
        return;
    }

    int num_rows = PQntuples(res);
    if (num_rows > 0) {
        char response[4096] = {0};
        strcat(response, "HTTP/1.1 200 OK\r\n");
        strcat(response, "Content-Type: application/json\r\n");
        strcat(response, "Access-Control-Allow-Origin: *\r\n");
        strcat(response, "\r\n");
        strcat(response, "{ \"auctions\": [");

        for (int i = 0; i < num_rows; i++) {
            char auction_item[512] = {0};
            sprintf(auction_item, "{ \"id\": %s, \"name\": \"%s\", \"seller\": \"%s\", \"duration\": \"%s\" }", PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
            strcat(response, auction_item);
            if (i < num_rows - 1) {
                strcat(response, ",");
            }
        }

        strcat(response, "] }");

        send(newSocket, response, strlen(response), 0);
    } else {
        const char *response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "Nessuna asta trovata";
        send(newSocket, response, strlen(response), 0);
    }

    PQclear(res);
}

int main() {
    // Connessione al database
    const char *DB_HOST = "database-progetto.cdce2824inp7.eu-west-3.rds.amazonaws.com";
    const char *DB_PORT = "5432";
    const char *DB_NAME = "postgres";
    const char *DB_USER = "postgres";
    const char *DB_PASS = "Progetto2324";

    char conninfo[256];
    snprintf(conninfo, sizeof(conninfo), "host=%s port=%s dbname=%s user=%s password=%s", DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASS);

    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connessione al database fallita: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return 1;
    } else {
        printf("Connessione al database avvenuta con successo\n");
    }

    // Creazione del socket del server
    int serverSocket, newSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[2048] = {0};

    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Failed to create socket\n");
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Bind failed\n");
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 3) < 0) {
        perror("Listen failed\n");
        close(serverSocket);
        return 1;
    }

    while (1) {
        if ((newSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen)) == -1) {
            perror("Accept failed\n");
            continue;
        }

        int valread = recv(newSocket, buffer, sizeof(buffer) - 1, 0);
        if (valread <= 0) {
            perror("Receive failed or connection closed\n");
            close(newSocket);
            continue;
        }

        buffer[valread] = '\0';

        // Gestisce la richiesta preflight OPTIONS
        if (strncmp(buffer, "OPTIONS", 7) == 0) {
            handle_options(newSocket);
        } else if (strncmp(buffer, "POST /login", 11) == 0) {  // Gestisce la richiesta di login
            handle_login(newSocket, buffer, conn);
        } else if (strncmp(buffer, "GET /auctions", 13) == 0) {  // Gestisce la richiesta di recupero delle aste
            handle_get_auctions(newSocket, conn);
        } else {
            printf("Client: %s\n", buffer);
        }

        close(newSocket);
    }

    close(serverSocket);
    PQfinish(conn);

    return 0;
}

