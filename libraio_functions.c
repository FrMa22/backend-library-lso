#include "libraio_functions.h"

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
#include <pthread.h>
#include <stdbool.h>


 
// Funzione per convertire la stringa booleana in un booleano
bool string_to_bool(const char *str) {
    printf("String libraio prelevata dal db :%s\n",str);
    return strcmp(str, "t") == 0;
}
 

void handle_get_libri(int newSocket, PGconn *conn) {
    printf("Entrato in handle_get_libri\n");
    // Recupero dei  libri dal database e invio al client
    PGresult *res = PQexec(conn, "SELECT titolo, autore, copie_totali,copie_in_prestito FROM libri ORDER BY titolo");
 
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
        strcat(response, "{ \"libri\": [");
 
        for (int i = 0; i < num_rows; i++) {
            char libro_item[512] = {0};
            sprintf(libro_item, "{ \"titolo\": \"%s\", \"autore\": \"%s\", \"copie_totali\": \"%s\", \"copie_in_prestito\": \"%s\" }", PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
            strcat(response, libro_item);
            printf("nome libro:%s\n",PQgetvalue(res,i,0));
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
            "Nessun libro trovato";
        send(newSocket, response, strlen(response), 0);
    }
 
    PQclear(res);
}
 
void handle_get_scaduti(int newSocket, PGconn *conn) {
    printf("Entrato in handle_get_scaduti\n");
    // Recupero dei  prestiti scaduti dal database e invio al client
    PGresult *res = PQexec(conn, "SELECT email, titolo, data_prestito, data_restituzione FROM prestiti WHERE data_restituzione < CURRENT_DATE ORDER BY data_restituzione");
 
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
        strcat(response, "{ \"scaduti\": [");
 
        for (int i = 0; i < num_rows; i++) {
            char scaduto_item[512] = {0};
            sprintf(scaduto_item, "{ \"email\": \"%s\", \"titolo\": \"%s\", \"data_prestito\": \"%s\", \"data_restituzione\": \"%s\" }", PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
            strcat(response, scaduto_item);
            printf("mail utente:%s  nome libro preso:%s ",PQgetvalue(res,i,0),PQgetvalue(res,i,1));
            if (i < num_rows - 1) {
                strcat(response, ",");
            }
        }
 
        strcat(response, "] }");
 
        printf("prestiti scaduti prelevati /n");
        send(newSocket, response, strlen(response), 0);
    } else {
        const char *response =
            "HTTP/1.1 202 No Content\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n";
            printf("nessun prestito scaduto trovato\n");
        send(newSocket, response, strlen(response), 0);
    }
 
    PQclear(res);
}
 
void get_limite_libri(int newSocket, PGconn *conn) {
    printf("entrato in getlimitelibri\n");
    // Recupero del limite di libri
    int limite_libri = -2; // Valore di default nel caso in cui il recupero del limite fallisca
 
    PGresult *limite_res = PQexec(conn, "SELECT valore_limite FROM limitePrestiti");
    if (PQresultStatus(limite_res) == PGRES_TUPLES_OK && PQntuples(limite_res) == 1) {
        limite_libri = atoi(PQgetvalue(limite_res, 0, 0));
    }
    PQclear(limite_res);
 
    // Creazione del JSON
    char json_body[512];
    snprintf(json_body, sizeof(json_body), "{ \"limite_libri\": %d }", limite_libri);
 
    printf("limite trovato: %d\n", limite_libri);
    // Costruzione della risposta HTTP
    char response[1024];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n"
             "%s", json_body);
 
    // Invio della risposta
    printf("inviato limite");
    send(newSocket, response, strlen(response),0);
}
 
void update_limite_libri(int newSocket, PGconn *conn, char *buffer) {
    printf("Entrato in update_limite_libri\n");
 
    // Estrai il nuovo limite dalla richiesta URL
    char *limite_start = strstr(buffer, "limite=");
    if (limite_start) {
        limite_start += strlen("limite=");
        int nuovo_limite = atoi(limite_start);
        printf("Valore nuovo limite:%d\n",nuovo_limite);
 
        if (nuovo_limite <= 0) {
            printf("Valore limite non valido\n");
 
            // Invia una risposta di errore
            const char *response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{ \"error\": \"Valore limite non valido\" }";
            send(newSocket, response, strlen(response), 0);
            return;
        }
 
        printf("Nuovo limite ricevuto: %d\n", nuovo_limite);
 
        // Aggiorna il limite nel database
        char query[512];
        snprintf(query, sizeof(query), "UPDATE limitePrestiti SET valore_limite = %d", nuovo_limite);
        PGresult *res = PQexec(conn, query);
 
        if (PQresultStatus(res) == PGRES_COMMAND_OK) {
            printf("Limite aggiornato con successo\n");
 
            // Invia una risposta di successo
            const char *response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{ \"message\": \"Limite aggiornato con successo\" }";
            send(newSocket, response, strlen(response), 0);
        } else {
            printf("Errore nell'aggiornamento del limite: %s\n", PQerrorMessage(conn));
 
            // Invia una risposta di errore
            const char *response =
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{ \"error\": \"Errore nell'aggiornamento del limite\" }";
            send(newSocket, response, strlen(response), 0);
        }
 
        PQclear(res);
    } else {
        // Invia una risposta di errore se il parametro limite non è presente nella richiesta URL
        printf("Parametro limite mancante\n");
 
        const char *response =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{ \"error\": \"Parametro limite mancante\" }";
        send(newSocket, response, strlen(response), 0);
    }
}
 
void handle_get_info(int newSocket, PGconn *conn,const char *titolo) {
    printf("Entrato in handle_get_info\n");
    // Recupero informazioni  libro dal database e invio al client
   
    char query[512];
    snprintf(query, sizeof(query), "SELECT email, titolo, data_prestito, data_restituzione FROM prestiti WHERE titolo='%s'", titolo);
 
    PGresult *res = PQexec(conn, query);
    //PGresult *res = PQexec(conn, "SELECT email,titolo,data_prestito,data_restituzione FROM prestiti WHERE titolo='%s' ",titolo);
 
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
        strcat(response, "{ \"prestiti\": [");
 
        for (int i = 0; i < num_rows; i++) {
            char info_item[512] = {0};
            sprintf(info_item, "{ \"email\": \"%s\", \"titolo\": \"%s\", \"data_prestito\": \"%s\", \"data_restituzione\": \"%s\" }", PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
            strcat(response, info_item);
            printf("mail utente:%s  nome libro preso:%s ",PQgetvalue(res,i,0),PQgetvalue(res,i,1));
            if (i < num_rows - 1) {
                strcat(response, ",");
            }
        }
 
        strcat(response, "] }");
         printf("prestiti trovati\n");
        send(newSocket, response, strlen(response), 0);
    } else {
        const char *response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "Nessun prestito trovato";
            printf("nessun prestito trovato\n");
        send(newSocket, response, strlen(response), 0);
    }
 
    PQclear(res);
}

void invia_messaggio(int newSocket, PGconn *conn,const char *titolo,const char *email,const char *data_scadenza) {
    printf("Entrato in invia_messaggio\n");
    // Recupero informazioni  libro dal database e invio al client

    // Stampa i valori estratti (opzionale, per debug)
        printf("Titolo: %s\n", titolo);
        printf("Email: %s\n", email);
        printf("Scadenza: %s\n", data_scadenza);

     // Costruisce il messaggio per la notifica
    char messaggio[300];
    snprintf(messaggio, sizeof(messaggio), "Attenzione! Il prestito per il libro %s è scaduto il giorno %s. Si prega di restituire il libro.", titolo, data_scadenza);
    printf("Messaggio:%s\n",messaggio);
    // Costruisce la query INSERT per inserire la notifica nella tabella "notifiche"
    char query[512];
    snprintf(query, sizeof(query), "INSERT INTO notifiche (messaggio, email_utente) VALUES ('%s', '%s')", messaggio, email);
    printf("Query:%s\n",query);
PGresult *res = PQexec(conn, query);

if (PQresultStatus(res) == PGRES_COMMAND_OK) {
    // Inserimento riuscito
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "{ \"message\": \"Notifica inserita correttamente\" }";
        printf("Notifica inviata al client\n");
    send(newSocket, response, strlen(response), 0);
} else {
    // Errore nell'inserimento
    const char *response =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "{ \"message\": \"Errore nell'inserimento della notifica\" }";
    printf("Errore creazione notifica per il client\n");
    send(newSocket, response, strlen(response), 0);
}

PQclear(res);
}
