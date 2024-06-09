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

#include "utente_functions.h"
#include "libraio_functions.h"

#define PORT 8080

pthread_mutex_t registrazione_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_options(int newSocket) {
    const char *response =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, GET, OPTIONS, PUT\r\n"  
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "\r\n";
    ssize_t bytes_sent=send(newSocket, response, strlen(response), 0);
     if (bytes_sent == -1) {
        perror("send failed\n");
    } 
}


void handle_login(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr,"Entrato in handle_login\n");
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
        snprintf(query, sizeof(query), "SELECT libraio FROM utenti WHERE email='%s' AND password='%s'", email, password);

        // Stampa la query con i valori correnti
        printf("Query eseguita: %s\n", query);

        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            printf("Utente trovato\n");
            int libraio = strcmp(PQgetvalue(res, 0, 0), "t") == 0;

            // Costruisci la risposta HTTP
            char response[4096];
            strcpy(response, "HTTP/1.1 200 OK\r\n");
            strcat(response, "Content-Type: text/plain\r\n");
            strcat(response, "Access-Control-Allow-Origin: *\r\n");
            strcat(response, "\r\n");
            if (libraio) {
                strcat(response, "libraio");
            } else {
                strcat(response, "utente");
            }
            ssize_t bytes_sent=send(newSocket, response, strlen(response), 0);
            if (bytes_sent == -1) {
                perror("send failed\n");
            } 
        } else {
            printf("Utente non trovato\n");

            // Costruisci la risposta HTTP
            const char *response = 
                "HTTP/1.1 401 Unauthorized\r\n"
                "Content-Type: text/plain\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "Credenziali non valide";
            ssize_t bytes_sent=send(newSocket, response, strlen(response), 0);
            if (bytes_sent == -1) {
                perror("send failed\n");
            }  
        }
        PQclear(res);
        json_decref(json_body);
    }
}

void print_books_found(PGresult *res) {
    int num_rows = PQntuples(res);
    printf("Libri trovati:\n");
    for (int i = 0; i < num_rows; i++) {
        printf("Titolo: %s, Autore: %s, Genere: %s, Copie Totali: %s, Copie in Prestito: %s\n",
               PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2),
               PQgetvalue(res, i, 3), PQgetvalue(res, i, 4));
    }
}




void handle_registration(int newSocket, char *buffer, PGconn *conn) {
    printf("Registrando nuovo utente\n");


     // Bloccare il mutex prima di eseguire la registrazione
    pthread_mutex_lock(&registrazione_mutex);

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body_req = json_loads(body, 0, &error);
        if (!json_body_req) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            pthread_mutex_unlock(&registrazione_mutex); // Sbloccare il mutex in caso di errore
            return;
        }

        // Estrai i campi dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_body_req, "email"));
        const char *password = json_string_value(json_object_get(json_body_req, "password"));
        if (!email || !password) {
            printf("Email o password mancanti nel corpo JSON\n");
            json_decref(json_body_req);
            pthread_mutex_unlock(&registrazione_mutex); // Sbloccare il mutex in caso di errore
            return;
        }
        printf("email: %s e password: %s ricevute\n", email, password);

        // Verifica se l'email è già presente nel database
        PGresult *check_res = PQexecParams(conn, "SELECT 1 FROM utenti WHERE email = $1", 1, NULL, &email, NULL, NULL, 0);
        if (PQresultStatus(check_res) != PGRES_TUPLES_OK || PQntuples(check_res) > 0) {
            printf("L'email %s è già presente nel database\n", email);
            PQclear(check_res);
            json_decref(json_body_req);

            // Invia la risposta 202 al frontend
            const char *response = 
            "HTTP/1.1 202 Accepted\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{ \"success\": false, \"message\": \"L'email è già presente nel database\" }";
            ssize_t bytes_sent=send(newSocket, response, strlen(response), 0);
            if (bytes_sent == -1) {
                perror("send failed\n");
            }
            pthread_mutex_unlock(&registrazione_mutex); // Sbloccare il mutex in caso di errore
            return;
        }
        PQclear(check_res);

        // Esegui la query per l'inserimento del nuovo utente nel database
        const char *query = "INSERT INTO utenti (email, password) VALUES ($1, $2)";
        const char *paramValues[2];
        paramValues[0] = email;
        paramValues[1] = password;

        PGresult *res = PQexecParams(conn, query, 2, NULL, paramValues, NULL, NULL, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            printf("Errore durante l'inserimento dell'utente nel database: %s\n", PQerrorMessage(conn));
            PQclear(res);
            json_decref(json_body_req);
            pthread_mutex_unlock(&registrazione_mutex); // Sbloccare il mutex in caso di errore
            return;
        }
        PQclear(res);

        // Invia il messaggio "utente registrato con successo" al frontend
        const char *response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "{ \"success\": true }";
        ssize_t bytes_sent=send(newSocket, response, strlen(response), 0);
            if (bytes_sent == -1) {
                perror("send failed\n");
            } else {
            printf("Registrazione avvenuta con successo per l'utente con email: %s\n", email);
            }  
        // Libera la memoria del corpo JSON della richiesta
        json_decref(json_body_req);
    }
    pthread_mutex_unlock(&registrazione_mutex); // Sbloccare il mutex dopo aver completato correttamente la registrazione
}


 




typedef struct {
    int socket;
    char db_host[256];
    char db_port[6];
    char db_name[256];
    char db_user[256];
    char db_pass[256];
} thread_data_t;

void *handle_client(void *arg) {


    thread_data_t *data = (thread_data_t *)arg;
    int newSocket = data->socket;

    // Buffer sufficientemente grande per contenere i dati di connessione
    char conninfo[1024];
    size_t conninfo_len = sizeof(conninfo);
    snprintf(conninfo, conninfo_len, "host=");
    strncat(conninfo, data->db_host, conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, " port=", conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, data->db_port, conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, " dbname=", conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, data->db_name, conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, " user=", conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, data->db_user, conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, " password=", conninfo_len - strlen(conninfo) - 1);
    strncat(conninfo, data->db_pass, conninfo_len - strlen(conninfo) - 1);

    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connessione al database fallita: %s", PQerrorMessage(conn));
        if (close(newSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
        free(data);
        return NULL;
    }

    char buffer[2048] = {0};

    int valread = recv(newSocket, buffer, sizeof(buffer) - 1, 0);
    if (valread <= 0) {
        perror("Receive failed or connection closed\n");
        if (close(newSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
        PQfinish(conn);
        free(data);
        return NULL;
    }

    buffer[valread] = '\0';

    // Gestisce la richiesta preflight OPTIONS
    if (strncmp(buffer, "OPTIONS", 7) == 0) {
        printf("Client: options");
        handle_options(newSocket);
    } else if (strncmp(buffer, "POST /login", 11) == 0) {  // Gestisce la richiesta di login
        printf("Client: login");
        handle_login(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /books", 11) == 0) {  // Gestisce la richiesta di recupero delle aste
        printf("Client: home");
        handle_get_books(newSocket, conn, buffer);
    } else if (strncmp(buffer, "POST /carrello", 14) == 0) {  // Gestisce la richiesta di recupero delle aste
        printf("Client: carrello");
        handle_get_carrello(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /search_books", 18) == 0) {
        fprintf(stderr, "Client: ricerca");
        handle_search_books(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /addToCart", 15) == 0) {  // Gestisce la richiesta di aggiunta al carrello
        printf("Client: aggiungi al carrello\n");
        handle_add_to_cart(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /removeFromCart", 20) == 0) {
        handle_remove_temporarily_from_cart(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /restoreToCart", 19) == 0) {
        handle_restore_to_cart(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /creaPrestiti", 18) == 0) {
        handle_crea_prestiti(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /get_limite_libri_per_utente", 20) == 0) {
        printf("Client: getLimiteLibri\n");
        get_limite_libri_per_utente(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /handle_get_prestiti_per_utente", 36) == 0) {
        handle_get_prestiti_per_utente(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /restituisci_libro", 23) == 0) {
        restituisci_libro(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /rimuovi_definitivamente_carrello", 38) == 0) {
        rimuovi_definitivamente_carrello(newSocket, buffer, conn);
    } else if (strncmp(buffer, "POST /registrazioneUtente", 25) == 0) {
        handle_registration(newSocket, buffer, conn);
    }else if (strncmp(buffer, "POST /check_notifiche", 21) == 0) {
        printf("ricevuta richiesta per notifiche");
        check_notifiche(newSocket, buffer, conn);
    }else if (strncmp(buffer, "POST /rimuovi_notifica", 22) == 0) {
        printf("ricevuta richiesta per rimuovi_notifica");
        rimuovi_notifica(newSocket, buffer, conn);
    }else if (strncmp(buffer, "POST /messaggio", 15) == 0) {  // Gestisce l'invio di un messaggio
        printf("\n\nentrato in manda avviso\n");
        // Cerca l'inizio del corpo della richiesta
        char *body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) {
            body_start += 4; // Avanza oltre la sequenza "\r\n\r\n"
            // Parsing del corpo della richiesta JSON
            json_error_t error;
            json_t *json_body = json_loads(body_start, 0, &error);
            if (!json_body) {
                printf("Errore nel parsing del corpo JSON: %s\n", error.text);
                return NULL; 
            }

            // Estrai i valori dal JSON
            const char *titolo = json_string_value(json_object_get(json_body, "titolo"));
            const char *email = json_string_value(json_object_get(json_body, "email"));
            const char *scadenza = json_string_value(json_object_get(json_body, "scadenza"));

            // Stampa i valori estratti (opzionale, per debug)
            printf("Titolo in post/messaggio: %s\n", titolo);
            printf("Email: %s\n", email);
            printf("Scadenza: %s\n", scadenza);

            // Invia il messaggio
            invia_messaggio(newSocket, conn, titolo, email, scadenza);

            // Libera la memoria allocata per il JSON
            json_decref(json_body);
        } else {
            // Errore nel trovare il corpo della richiesta
            printf("Errore nel trovare il corpo della richiesta\n");
        }
     }else {
        printf("Client: %s\n", buffer);
    }

    if (strncmp(buffer, "POST /login", 11) == 0) {  // Gestisce la richiesta di login
        handle_login(newSocket, buffer, conn);
    } else if (strncmp(buffer, "GET /libri", 10) == 0) {  // Gestisce la richiesta di recupero dei libri
        handle_get_libri(newSocket, conn);
    }else if (strncmp(buffer, "GET /scaduti", 12) == 0) {  // Gestisce la richiesta di recupero dei libri
        handle_get_scaduti(newSocket, conn);
    }else if (strncmp(buffer, "GET /limite_libri", 17) == 0) {  // Gestisce la richiesta di recupero dei libri
        get_limite_libri(newSocket, conn);
    }else if (strncmp(buffer, "PUT /limite_libri", 17) == 0) {  // Gestisce la richiesta di aggiornamento del limite
        update_limite_libri(newSocket, conn, buffer);
    }else if (strncmp(buffer, "GET /info", 9) == 0) {  // Gestisce la richiesta di recupero informazioni
                // Estrae il titolo dalla richiesta GET
        char *titolo_start = strstr(buffer, "titolo=");
        if (titolo_start) {
            titolo_start += strlen("titolo=");
            char *titolo_end = strchr(titolo_start, ' '); // Trova il primo spazio dopo "titolo="
            if (titolo_end) {
                *titolo_end = '\0'; // Termina la stringa al primo spazio dopo "titolo="
            }
            // Sostituisci "%20" con spazi regolari nel titolo
            char *ptr = titolo_start;
            while ((ptr = strstr(ptr, "%20"))) {
                *ptr = ' ';
                memmove(ptr + 1, ptr + 3, strlen(ptr + 3) + 1);
                //la funzione memmove sposta un blocco di memoria all'interno della stringa 
                // destinazione:ptr + 1 - questa è la posizione subito dopo lo spazio che abbiamo appena messo.
                //origine:ptr + 3 - questa è la posizione subito dopo "20", ossia l'inizio del resto della stringa.
                //Numero di byte da copiare: strlen(ptr + 3) + 1 lunghezza della parte rimanente della stringa, più uno per includere il terminatore nullo '\0'.
            }
            char titolo[256];
            strcpy(titolo, titolo_start);
            printf("Titolo: %s\n", titolo);
            handle_get_info(newSocket, conn, titolo);
        }
    }
        
        
        

    if (close(newSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
    PQfinish(conn);
    free(data);
    return NULL;
}


int main() {
    // Parametri di connessione al database
    const char *DB_HOST = "lso.cn0yyy24sf56.eu-north-1.rds.amazonaws.com";
    const char *DB_PORT = "5432";
    const char *DB_NAME = "postgres";
    const char *DB_USER = "postgres";
    const char *DB_PASS = "Progetto2324";

    // Creazione del socket del server e del socket utilizzato per le connessioni tra server e client
    int serverSocket, newSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Failed to create socket\n");
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Bind failed\n");
       if (close(serverSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
        return 1;
    }

    if (listen(serverSocket, 3) < 0) {
        perror("Listen failed\n");
       if (close(serverSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
        return 1;
    }

    while (1) {
        if ((newSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen)) == -1) {
            perror("Accept failed\n");
            continue;
        }

        thread_data_t *data = malloc(sizeof(thread_data_t));
        if (!data) {
            perror("Failed to allocate memory\n");
            if (close(newSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
            continue;
        }
        data->socket = newSocket;
        strncpy(data->db_host, DB_HOST, sizeof(data->db_host));
        strncpy(data->db_port, DB_PORT, sizeof(data->db_port));
        strncpy(data->db_name, DB_NAME, sizeof(data->db_name));
        strncpy(data->db_user, DB_USER, sizeof(data->db_user));
        strncpy(data->db_pass, DB_PASS, sizeof(data->db_pass));

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, (void *)data) != 0) {
            perror("Failed to create thread\n");
            if (close(newSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }
            free(data);
            continue;
        }

       int r=pthread_detach(client_thread);//istruzione importante per evitare grossi rallentamenti al server C
        if(r!=0){
            perror("Errore detach thread\n");
        }
    }

    if (close(serverSocket) == -1) {
            perror("Errore durante la chiusura del socket");
        }

    return 0;
}

//    gcc -o main main.c -I/usr/include/postgresql -L/usr/lib/x86_64-linux-gnu -ljansson -lpq
//    gcc -o main main.c utente_functions.c libraio_functions.c -I/usr/include/postgresql -L/usr/lib/x86_64-linux-gnu -ljansson -lpq

