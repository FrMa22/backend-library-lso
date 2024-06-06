#include "utente_functions.h"

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

// Dichiarazione del mutex come variabile globale
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void handle_get_books(int newSocket, PGconn *conn, char *buffer) {
    fprintf(stderr, "Entrato in handle_get_books\n");
        
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }
        const char *email = json_string_value(json_object_get(json_body, "email"));
        printf("Email ricevuta: %s\n", email);

        // Recupero dei libri dal database
        char query[512];
        snprintf(query, sizeof(query), "SELECT libri.titolo, libri.autore, libri.genere, libri.copie_totali - COALESCE(libri.copie_in_prestito, 0) AS copie_disponibili, libri.durata_prestito, (carrello.titolo IS NOT NULL) AS presente_nel_carrello FROM libri LEFT JOIN carrello ON libri.titolo = carrello.titolo AND carrello.email = '%s' ORDER BY RANDOM() LIMIT 5", email);

        PGresult *res = PQexec(conn, query);

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
            strcat(response, "{ \"books\": [");

            for (int i = 0; i < num_rows; i++) {
                char total_copies_str[20];
                snprintf(total_copies_str, sizeof(total_copies_str), "%d", atoi(PQgetvalue(res, i, 3)));
                char durata_prestito[20];
                snprintf(durata_prestito, sizeof(durata_prestito), "%d", atoi(PQgetvalue(res, i, 4)));

                fprintf(stderr, "\nValore di entrato nel carrello per il libro :  %s, %s", PQgetvalue(res,i,0), PQgetvalue(res,i,5));
                char book_item[512] = {0};
                snprintf(book_item, sizeof(book_item), "{ \"titolo\": \"%s\", \"autore\": \"%s\", \"genere\": \"%s\", \"copie_totali\": \"%s\" , \"durata_prestito\": \"%s\", \"presente_nel_carrello\": \"%s\" }",
                PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), total_copies_str, durata_prestito, PQgetvalue(res, i, 5));
                strcat(response, book_item);
                if (i < num_rows - 1) {
                    strcat(response, ",");
                }
            }
            strcat(response, "] }");
            send(newSocket, response, strlen(response), 0);
        }
        PQclear(res);
        json_decref(json_body);
    }
}

void handle_get_carrello(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in handle_get_carrello\n");

 // Recupero del limite di libri
    int limite_libri;
    PGresult *limite_res = PQexec(conn, "SELECT valore_limite FROM limitePrestiti");
    if (PQresultStatus(limite_res) != PGRES_TUPLES_OK || PQntuples(limite_res) != 1) {
        fprintf(stderr, "Errore nel recupero del limite di libri");
        PQclear(limite_res);
        return;
    }
    limite_libri = atoi(PQgetvalue(limite_res, 0, 0));
    PQclear(limite_res);

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email dal corpo JSON
        const char *email = json_string_value(json_object_get(json_body, "email"));
        if (!email) {
            printf("Email mancante nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);

        // Recupero dei libri nel carrello dal database, inclusa l'informazione "rimosso_temporaneamente"
        char query[512];
        snprintf(query, sizeof(query), 
                 "SELECT libri.titolo, libri.autore, libri.genere, libri.copie_totali, libri.copie_in_prestito, libri.durata_prestito, carrello.rimosso_temporaneamente "
                 "FROM carrello "
                 "JOIN libri ON carrello.titolo = libri.titolo "
                 "WHERE carrello.email = '%s'", email);

        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Query fallita: %s", PQerrorMessage(conn));
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
            strcat(response, "{ \"books\": [");

            for (int i = 0; i < num_rows; i++) {
                char total_copies_str[20];
                snprintf(total_copies_str, sizeof(total_copies_str), "%d", atoi(PQgetvalue(res, i, 3)));
                char copies_in_prestito_str[20];
                snprintf(copies_in_prestito_str, sizeof(copies_in_prestito_str), "%d", atoi(PQgetvalue(res, i, 4)));
                char durata_prestito[20];
                snprintf(durata_prestito, sizeof(durata_prestito), "%d", atoi(PQgetvalue(res, i, 5)));
                char rimosso_temporaneamente[2];
                snprintf(rimosso_temporaneamente, sizeof(rimosso_temporaneamente), "%d", atoi(PQgetvalue(res, i, 6)));

                fprintf(stderr, ("Valore di rimosso_temporaneamente del libro %s è %s", PQgetvalue(res,i,0), PQgetvalue(res,i,6)) , query);
                char book_item[512] = {0};
                snprintf(book_item, sizeof(book_item), 
                         "{ \"titolo\": \"%s\", \"autore\": \"%s\", \"genere\": \"%s\", \"copie_totali\": \"%s\", \"copie_in_prestito\": \"%s\", \"durata_prestito\": \"%s\", \"rimosso_temporaneamente\": \"%s\" }",
                         PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), total_copies_str, copies_in_prestito_str, durata_prestito, PQgetvalue(res, i, 6));
                strcat(response, book_item);
                if (i < num_rows - 1) {
                    strcat(response, ",");
                }
            }
            strcat(response, "] }");
            send(newSocket, response, strlen(response), 0);
        } else {
            const char *response = 
                "HTTP/1.1 204 \r\n"
                "Content-Type: text/plain\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                    "{ \"message\": \"Nessun libro trovato nel carrello\" }";
            send(newSocket, response, strlen(response), 0);
        }

        PQclear(res);
        json_decref(json_body);
    }
}

void handle_search_books(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in handle_search_books\n");
    
    // Gestisce la richiesta di ricerca dei libri

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        printf("Corpo della richiesta trovato\n");
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email, il nome e le categorie dal corpo JSON
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *name = json_string_value(json_object_get(json_body, "name"));
        const char *category = json_string_value(json_object_get(json_body, "category"));
        
        fprintf(stderr,"Email ricevuta: %s\n", email);
        fprintf(stderr,"Nome ricevuto: %s\n", name ? name : "null");
        fprintf(stderr,"Categoria ricevuta: %s\n", category ? category : "null");

        // Costruisci la query per cercare i libri
        char query[512];

        if (name && strlen(name) > 0 && category && strlen(category) > 0) {
            snprintf(query, sizeof(query),
                     "SELECT libri.titolo, libri.autore, libri.genere, libri.copie_totali - COALESCE(libri.copie_in_prestito, 0) AS copie_disponibili, libri.durata_prestito, "
                     "(carrello.titolo IS NOT NULL) AS presente_nel_carrello "
                     "FROM libri LEFT JOIN carrello ON libri.titolo = carrello.titolo AND carrello.email = '%s' "
                     "WHERE libri.titolo ILIKE '%%%s%%' OR libri.genere ILIKE '%%%s%%'", email, name, category);
        } else if (name && strlen(name) > 0) {
            snprintf(query, sizeof(query),
                     "SELECT libri.titolo, libri.autore, libri.genere, libri.copie_totali - COALESCE(libri.copie_in_prestito, 0) AS copie_disponibili, libri.durata_prestito, "
                     "(carrello.titolo IS NOT NULL) AS presente_nel_carrello "
                     "FROM libri LEFT JOIN carrello ON libri.titolo = carrello.titolo AND carrello.email = '%s' "
                     "WHERE libri.titolo ILIKE '%%%s%%'", email, name);
        } else if (category && strlen(category) > 0) {
            snprintf(query, sizeof(query),
                     "SELECT libri.titolo, libri.autore, libri.genere, libri.copie_totali - COALESCE(libri.copie_in_prestito, 0) AS copie_disponibili, libri.durata_prestito, "
                     "(carrello.titolo IS NOT NULL) AS presente_nel_carrello "
                     "FROM libri LEFT JOIN carrello ON libri.titolo = carrello.titolo AND carrello.email = '%s' "
                     "WHERE libri.genere ILIKE '%%%s%%'", email, category);
        } else {
            const char *response = 
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "Nome o categoria devono essere specificati";
            send(newSocket, response, strlen(response), 0);
            json_decref(json_body);
            return;
        }

        fprintf(stderr, "Query eseguita: %s\n", query);

        // Esegui la query
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            char response[4096] = {0};
            strcat(response, "HTTP/1.1 200 OK\r\n");
            strcat(response, "Content-Type: application/json\r\n");
            strcat(response, "Access-Control-Allow-Origin: *\r\n");
            strcat(response, "\r\n");
            strcat(response, "{ \"books\": [");

            for (int i = 0; i < PQntuples(res); i++) {
                char total_copies_str[20];
                snprintf(total_copies_str, sizeof(total_copies_str), "%d", atoi(PQgetvalue(res, i, 3)));
                char durata_prestito[20];
                snprintf(durata_prestito, sizeof(durata_prestito), "%d", atoi(PQgetvalue(res, i, 4)));

                fprintf(stderr, "\nValore di entrato nel carrello per il libro: %s, %s\n", PQgetvalue(res,i,0), PQgetvalue(res,i,5));
                char book_item[512] = {0};
                snprintf(book_item, sizeof(book_item), "{ \"titolo\": \"%s\", \"autore\": \"%s\", \"genere\": \"%s\", \"copie_totali\": \"%s\", \"durata_prestito\": \"%s\", \"presente_nel_carrello\": \"%s\" }",
                PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), total_copies_str, durata_prestito, PQgetvalue(res, i, 5));
                strcat(response, book_item);
                if (i < PQntuples(res) - 1) {
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
        json_decref(json_body);
    }
}

void handle_add_to_cart(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr,"Entrato in handle_add_to_cart\n");
    
        
        
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email e il titolo del libro
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *bookTitle = json_string_value(json_object_get(json_body, "bookTitle"));
        if (!email || !bookTitle) {
            printf("Email o titolo del libro mancanti nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);
        printf("Titolo del libro ricevuto: %s\n", bookTitle);

        
        // Controlla se il libro ha copie disponibili
        char query[512];
        snprintf(query, sizeof(query), "SELECT copie_totali - copie_in_prestito AS copie_disponibili FROM libri WHERE titolo = '%s'", bookTitle);

        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            printf("Errore nella query di controllo delle copie disponibili: %s\n", PQerrorMessage(conn));
            PQclear(res);
            json_decref(json_body);
            return;
        }

        int copieDisponibili = atoi(PQgetvalue(res, 0, 0));

        if (copieDisponibili > 0) {
            // Esegui l'inserimento nel carrello
            snprintf(query, sizeof(query), "INSERT INTO carrello (email, titolo) VALUES ('%s', '%s')", email, bookTitle);

            PQclear(res);
            res = PQexec(conn, query);

            if (PQresultStatus(res) == PGRES_COMMAND_OK) {
                // Costruisci la risposta HTTP
                const char *response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "\r\n"
                    "{ \"success\": true }";
                send(newSocket, response, strlen(response), 0);
            } else {
                printf("Errore nell'inserimento nel carrello: %s\n", PQerrorMessage(conn));

                // Costruisci la risposta HTTP
                const char *response = 
                    "HTTP/1.1 500 Internal Server Error\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "\r\n"
                    "{ \"success\": false, \"message\": \"Errore durante l'inserimento nel carrello\" }";
                send(newSocket, response, strlen(response), 0);
            }
        } else {
            printf("Il libro non ha copie disponibili\n");

            // Costruisci la risposta HTTP
            const char *response = 
                "HTTP/1.1 202 \r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{ \"success\": false, \"message\": \"Il libro non ha copie disponibili\" }";
            send(newSocket, response, strlen(response), 0);
        }

        PQclear(res);
        json_decref(json_body);
    }
}


void handle_remove_temporarily_from_cart(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in handle_remove_temporarily_from_cart\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email e il titolo del libro
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *titolo = json_string_value(json_object_get(json_body, "titolo"));
        if (!email || !titolo) {
            printf("Email o titolo mancanti nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);
        printf("Titolo ricevuto: %s\n", titolo);

        // Imposta il flag rimosso_temporaneamente a true nel database
        char query[512];
        snprintf(query, sizeof(query), "UPDATE carrello SET rimosso_temporaneamente = true WHERE email='%s' AND titolo='%s'", email, titolo);

        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
            PQclear(res);
            return;
        }

        // Costruisci la risposta HTTP
        const char *response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "Libro rimosso temporaneamente dal carrello";
        send(newSocket, response, strlen(response), 0);

        PQclear(res);
        json_decref(json_body);
    }
}

void handle_restore_to_cart(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in handle_restore_to_cart\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email e il titolo del libro
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *titolo = json_string_value(json_object_get(json_body, "titolo"));
        if (!email || !titolo) {
            printf("Email o titolo mancanti nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);
        printf("Titolo ricevuto: %s\n", titolo);

        // Imposta il flag rimosso_temporaneamente a false nel database
        char query[512];
        snprintf(query, sizeof(query), "UPDATE carrello SET rimosso_temporaneamente = false WHERE email='%s' AND titolo='%s'", email, titolo);

        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
            PQclear(res);
            return;
        }

        // Costruisci la risposta HTTP
        const char *response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "Libro ripristinato nel carrello";
        send(newSocket, response, strlen(response), 0);

        PQclear(res);
        json_decref(json_body);
    }
}

char** controlla_copie_disponibili(int newSocket, char *buffer, PGconn *conn, int *num_books) {
    fprintf(stderr, "Entrato in controlla_copie_disponibili\n");

    // Estrai l'email dal buffer della richiesta JSON
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_obj = json_loads(body, 0, &error);
        if (!json_obj) {
            fprintf(stderr, "Errore nel parsing del corpo JSON: %s\n", error.text);
            return NULL;
        }

        // Estrai l'email dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_obj, "email"));
        if (!email) {
            fprintf(stderr, "Email mancante nel corpo JSON\n");
            json_decref(json_obj);
            return NULL;
        }

        char query[512];
        snprintf(query, sizeof(query), "SELECT l.titolo FROM carrello c JOIN libri l ON c.titolo = l.titolo WHERE c.email = '%s' AND NOT c.rimosso_temporaneamente AND l.copie_totali = l.copie_in_prestito", email);
        
        // Stampa la query con i valori correnti
        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
            PQclear(res);
            json_decref(json_obj);
            return NULL;
        }

        int rows = PQntuples(res);
        if (rows == 0) {
            printf("Nessun libro nel carrello dell'utente con copie disponibili <= 0\n");
            PQclear(res);
            json_decref(json_obj);
            *num_books = 0;
            return NULL;
        } else {
            char **books_without_copies = malloc((rows + 1) * sizeof(char*));
            if (books_without_copies == NULL) {
                fprintf(stderr, "Errore di allocazione di memoria\n");
                PQclear(res);
                json_decref(json_obj);
                return NULL;
            }
            for (int i = 0; i < rows; ++i) {
                const char *titolo = PQgetvalue(res, i, 0);
                books_without_copies[i] = strdup(titolo);
                printf("%s\n", titolo); // Stampa il titolo del libro rimosso
                const char *remove_query = "DELETE FROM carrello WHERE email = $1 AND titolo = $2";
                const char *remove_params[2] = { email, titolo };
                PQexecParams(conn, remove_query, 2, NULL, remove_params, NULL, NULL, 0);
            }
            books_without_copies[rows] = NULL; // Aggiungi il terminatore NULL
            PQclear(res);
            json_decref(json_obj);
            *num_books = rows;
            return books_without_copies;
        }
    } else {
        fprintf(stderr, "Corpo della richiesta JSON non trovato\n");
        return NULL;
    }
}

char** controlla_libri_gia_presi_in_prestito(int newSocket, char *buffer, PGconn *conn, int *num_books) {
    fprintf(stderr, "Entrato in controlla_libri_gia_presi_in_prestito\n");

    // Estrai l'email dal buffer della richiesta JSON
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_obj = json_loads(body, 0, &error);
        if (!json_obj) {
            fprintf(stderr, "Errore nel parsing del corpo JSON: %s\n", error.text);
            return NULL;
        }

        // Estrai l'email dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_obj, "email"));
        if (!email) {
            fprintf(stderr, "Email mancante nel corpo JSON\n");
            json_decref(json_obj);
            return NULL;
        }

        // Costruisci la query per controllare i libri già presi in prestito che sono nel carrello dell'utente
        char query[512];
        snprintf(query, sizeof(query), 
                 "SELECT c.titolo FROM carrello c JOIN prestiti p ON c.email = p.email AND c.titolo = p.titolo "
                 "WHERE c.email = '%s'", email);

        // Esegui la query
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
            PQclear(res);
            json_decref(json_obj);
            return NULL;
        }

        int rows = PQntuples(res);
        if (rows == 0) {
            printf("Nessun libro già preso in prestito nel carrello dell'utente\n");
            PQclear(res);
            json_decref(json_obj);
            *num_books = 0;
            return NULL;
        } else {
            char **books_already_borrowed_in_cart = malloc((rows + 1) * sizeof(char*));
            if (books_already_borrowed_in_cart == NULL) {
                fprintf(stderr, "Errore di allocazione di memoria\n");
                PQclear(res);
                json_decref(json_obj);
                return NULL;
            }
            for (int i = 0; i < rows; ++i) {
                const char *titolo = PQgetvalue(res, i, 0);
                books_already_borrowed_in_cart[i] = strdup(titolo);
                printf("%s\n", titolo); // Stampa il titolo del libro già preso in prestito e presente nel carrello
                // Rimuovi il libro dal carrello
                const char *remove_query = "DELETE FROM carrello WHERE email = $1 AND titolo = $2";
                const char *remove_params[2] = { email, titolo };
                PQexecParams(conn, remove_query, 2, NULL, remove_params, NULL, NULL, 0);
            }
            books_already_borrowed_in_cart[rows] = NULL; // Aggiungi il terminatore NULL
            PQclear(res);
            json_decref(json_obj);
            *num_books = rows;
            return books_already_borrowed_in_cart;
        }
    } else {
        fprintf(stderr, "Corpo della richiesta JSON non trovato\n");
        return NULL;
    }
}

void handle_crea_prestiti(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in handle_crea_prestiti\n");
      // Entrata nella sezione critica
        pthread_mutex_lock(&mutex);

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            pthread_mutex_unlock(&mutex);
            return;
        }

        // Estrai l'email dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_body, "email"));
        if (!email) {
            printf("Email mancante nel corpo JSON\n");
            json_decref(json_body);
            pthread_mutex_unlock(&mutex);
            return;
        }
        fprintf(stderr, "Email recuperata : %s\n", email);

       

        int num_books;
        char **books_without_copies = controlla_copie_disponibili(newSocket, buffer, conn, &num_books);

        int num_books_already_borrowed_in_cart;
        char **books_already_borrowed_in_cart = controlla_libri_gia_presi_in_prestito(newSocket,buffer, conn, &num_books_already_borrowed_in_cart);


        

        if (num_books > 0 || num_books_already_borrowed_in_cart>0) {
            // Costruisci la risposta HTTP
            // Calcola la lunghezza necessaria per la stringa JSON
            int buffer_size = 1024; // Inizialmente un buffer di 1 KB, può essere adattato
            char *buffer = malloc(buffer_size);
            if (!buffer) {
                fprintf(stderr, "Errore nell'allocazione del buffer\n");
                pthread_mutex_unlock(&mutex);
                return;
            }

            if(num_books>0 && num_books_already_borrowed_in_cart==0){
                // Crea l'inizio della risposta
                snprintf(buffer, buffer_size, 
                    "HTTP/1.1 409 \r\n"
                    "Content-Type: text/plain\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "\r\n"
                    "{ \"success\": false, \"message\": \"Trovati libri con 0 copie: \", \"body\": [");

                // Aggiungi i valori dell'array `num_books` al body
                for (int i = 0; i < num_books; ++i) {
                    // Stampare il titolo del libro
                    printf("Libro inserito nel buffer: %s\n", books_without_copies[i]);

                    if (i > 0) {
                        strncat(buffer, ", ", buffer_size - strlen(buffer) - 1);
                    }
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                    strncat(buffer, books_without_copies[i], buffer_size - strlen(buffer) - 1);
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                    printf("Body attuale: %s\n", buffer);
                }

                // Completa la stringa JSON
                strncat(buffer, "] }", buffer_size - strlen(buffer) - 1);

                // Invia la risposta
                send(newSocket, buffer, strlen(buffer), 0);

                fprintf(stderr, "Mandato il JSON di cattiva riuscita\n");

                // Libera la memoria allocata
                free(buffer);
                for (int i = 0; i < num_books; ++i) {
                    free(books_without_copies[i]); // Libera la memoria allocata per ogni titolo
                }
                free(books_without_copies); // Libera l'array di titoli
            }else if(num_books==0 && num_books_already_borrowed_in_cart>0){
                // Crea l'inizio della risposta
                snprintf(buffer, buffer_size, 
                    "HTTP/1.1 409 \r\n"
                    "Content-Type: text/plain\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "\r\n"
                    "{ \"success\": false, \"message\": \"Trovati libri gia presi in prestito: \", \"body\": [");

                // Aggiungi i valori dell'array `num_books_already_borrowed_in_cart` al body
                for (int i = 0; i < num_books_already_borrowed_in_cart; ++i) {
                    // Stampare il titolo del libro
                    printf("Libro inserito nel buffer: %s\n", books_already_borrowed_in_cart[i]);

                    if (i > 0) {
                        strncat(buffer, ", ", buffer_size - strlen(buffer) - 1);
                    }
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                    strncat(buffer, books_already_borrowed_in_cart[i], buffer_size - strlen(buffer) - 1);
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                    printf("Body attuale: %s\n", buffer);
                }

                // Completa la stringa JSON
                strncat(buffer, "] }", buffer_size - strlen(buffer) - 1);

                // Invia la risposta
                send(newSocket, buffer, strlen(buffer), 0);

                fprintf(stderr, "Mandato il JSON di cattiva riuscita\n");

                // Libera la memoria allocata
                free(buffer);
                for (int i = 0; i < num_books; ++i) {
                    free(books_already_borrowed_in_cart[i]); // Libera la memoria allocata per ogni titolo
                }
                free(books_already_borrowed_in_cart); // Libera l'array di titoli
            }else{
                // Costruisci la risposta HTTP
                // Calcola la lunghezza necessaria per la stringa JSON
                int buffer_size = 2048; // Inizialmente un buffer di 2 KB, può essere adattato
                char *buffer = malloc(buffer_size);
                if (!buffer) {
                    fprintf(stderr, "Errore nell'allocazione del buffer\n");
                    pthread_mutex_unlock(&mutex);
                    return;
                }

                // Crea l'inizio della risposta
                snprintf(buffer, buffer_size, 
                    "HTTP/1.1 409 \r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "\r\n"
                    "{ \"success\": false, \"message\": \"Trovati libri con 0 copie e già in prestito nel carrello\", \"books_zero_copies\": [");

                // Aggiungi i valori dell'array `books_without_copies` al body
                for (int i = 0; i < num_books; ++i) {
                    // Aggiungi il titolo del libro
                    if (i > 0) {
                        strncat(buffer, ", ", buffer_size - strlen(buffer) - 1);
                    }
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                    strncat(buffer, books_without_copies[i], buffer_size - strlen(buffer) - 1);
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                }

                // Aggiungi la lista di libri già in prestito nel carrello
                strncat(buffer, "], \"books_already_borrowed\": [", buffer_size - strlen(buffer) - 1);
                for (int i = 0; i < num_books_already_borrowed_in_cart; ++i) {
                    // Aggiungi il titolo del libro
                    if (i > 0) {
                        strncat(buffer, ", ", buffer_size - strlen(buffer) - 1);
                    }
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                    strncat(buffer, books_already_borrowed_in_cart[i], buffer_size - strlen(buffer) - 1);
                    strncat(buffer, "\"", buffer_size - strlen(buffer) - 1);
                }

                // Completa la stringa JSON
                strncat(buffer, "] }", buffer_size - strlen(buffer) - 1);

                // Invia la risposta
                send(newSocket, buffer, strlen(buffer), 0);

                fprintf(stderr, "Mandato il JSON dei libri con copie zero o già in prestito nel carrello\n");

                // Libera la memoria allocata
                free(buffer);
                for (int i = 0; i < num_books; ++i) {
                    free(books_without_copies[i]); // Libera la memoria allocata per ogni titolo
                }
                free(books_without_copies); // Libera l'array di titoli

                for (int i = 0; i < num_books_already_borrowed_in_cart; ++i) {
                    free(books_already_borrowed_in_cart[i]); // Libera la memoria allocata per ogni titolo
                }
                free(books_already_borrowed_in_cart); // Libera l'array di titoli
            }
        } else {
            // Chiama la funzione del database per creare i prestiti
            const char *query = "SELECT crea_prestiti($1)";
            const char *params[1] = { email };
            PGresult *res = PQexecParams(conn, query, 1, NULL, params, NULL, NULL, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
                PQclear(res);
                pthread_mutex_unlock(&mutex);
                return;
            }

            PQclear(res);

            // Invia il messaggio "prestiti creati con successo" al frontend
            const char *response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{ \"success\": true }";
            send(newSocket, response, strlen(response), 0);
            fprintf(stderr, "Mandato il JSON di buona riuscita");
             // Uscita dalla sezione critica
        }
        json_decref(json_body);
        pthread_mutex_unlock(&mutex);
    }
}

void get_limite_libri_per_utente(int newSocket, char *buffer, PGconn *conn) {
    printf("entrato in getlimitelibri");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Salta i 4 caratteri "\r\n\r\n"

        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body_req = json_loads(body, 0, &error);
        if (!json_body_req) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai l'email dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_body_req, "email"));
        if (!email) {
            printf("Email mancante nel corpo JSON\n");
            json_decref(json_body_req);
            return;
        }
        fprintf(stderr, "Email recuperata : %s\n", email);

        // Recupero del limite di libri
        int limite_libri = -2; // Valore di default nel caso in cui il recupero del limite fallisca

        PGresult *limite_res = PQexec(conn, "SELECT valore_limite FROM limitePrestiti");
        if (PQresultStatus(limite_res) == PGRES_TUPLES_OK && PQntuples(limite_res) == 1) {
            limite_libri = atoi(PQgetvalue(limite_res, 0, 0));
        }
        PQclear(limite_res);

        // Recupero del numero di libri attualmente in prestito dall'utente
        int libri_in_prestito = -1; // Valore di default nel caso in cui il recupero dei libri in prestito fallisca

        PGresult *prestiti_res = PQexecParams(conn, "SELECT COUNT(*) FROM prestiti WHERE email = $1", 1, NULL, &email, NULL, NULL, 0);
        if (PQresultStatus(prestiti_res) == PGRES_TUPLES_OK && PQntuples(prestiti_res) == 1) {
            libri_in_prestito = atoi(PQgetvalue(prestiti_res, 0, 0));
        }
        PQclear(prestiti_res);

        // Calcola il limite effettivo di libri disponibili per l'utente
        int limite_effettivo = limite_libri - libri_in_prestito;

        // Creazione del JSON per la risposta
        char response_json_body[512];
        snprintf(response_json_body, sizeof(response_json_body), "{ \"limite_libri\": %d }", limite_effettivo);

        printf("limite trovato: %d", limite_effettivo);
        // Costruzione della risposta HTTP
        char response[1024];
        snprintf(response, sizeof(response), 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "%s", response_json_body);

        // Invio della risposta
        send(newSocket, response, strlen(response), 0);

        // Libera la memoria del corpo JSON della richiesta
        json_decref(json_body_req);
    }
}

void handle_get_prestiti_per_utente(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in handle_get_prestiti\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Skip the 4 characters "\r\n\r\n"

        // Parse the JSON body of the request
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Extract the email from the JSON body
        const char *email = json_string_value(json_object_get(json_body, "email"));
        if (!email) {
            printf("Email mancante nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);

        // Query to retrieve borrowed books for the user
        char query[512];
        snprintf(query, sizeof(query),
                 "SELECT titolo, data_prestito, data_restituzione "
                 "FROM prestiti "
                 "WHERE email = '%s'", email);

        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Query fallita: %s", PQerrorMessage(conn));
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
            strcat(response, "{ \"borrowed_books\": [");

            for (int i = 0; i < num_rows; i++) {
                char book_item[512] = {0};
                snprintf(book_item, sizeof(book_item),
                         "{ \"titolo\": \"%s\", \"data_prestito\": \"%s\", \"data_restituzione\": \"%s\" }",
                         PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2));
                strcat(response, book_item);
                if (i < num_rows - 1) {
                    strcat(response, ",");
                }
            }
            strcat(response, "] }");
            send(newSocket, response, strlen(response), 0);
        } else {
            const char *response =
                "HTTP/1.1 204 No Content\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{ \"message\": \"Nessun libro trovato nei prestiti\" }";
            send(newSocket, response, strlen(response), 0);
        }

        PQclear(res);
        json_decref(json_body);
    }
}

void restituisci_libro(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in restituisci_libro\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Skip the 4 characters "\r\n\r\n"

        // Parse the JSON body of the request
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Extract the email and book title from the JSON body
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *titolo = json_string_value(json_object_get(json_body, "titolo"));
        if (!email || !titolo) {
            printf("Email o titolo mancanti nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);
        printf("Titolo ricevuto: %s\n", titolo);

        // Query to remove the book from loans
        char query[512];
        snprintf(query, sizeof(query),
                 "DELETE FROM prestiti WHERE email = '%s' AND titolo = '%s'", email, titolo);

        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "Query fallita: %s", PQerrorMessage(conn));
            PQclear(res);
            return;
        }

        PQclear(res);
        json_decref(json_body);

        // Build the JSON response
        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{ \"message\": \"Libro restituito con successo\" }";
        send(newSocket, response, strlen(response), 0);
    }
}

void rimuovi_definitivamente_carrello(int newSocket, char *buffer, PGconn *conn) {


    fprintf(stderr, "Entrato in rimuovi_definitivamente_carrello\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Skip the 4 characters "\r\n\r\n"

        // Parse the JSON body of the request
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Extract the email and book title from the JSON body
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *titolo = json_string_value(json_object_get(json_body, "titolo"));
        if (!email || !titolo) {
            printf("Email o titolo mancanti nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);
        printf("Titolo ricevuto: %s\n", titolo);

        // Query to remove the book from loans
        char query[512];
        snprintf(query, sizeof(query),
                 "DELETE FROM carrello WHERE email = '%s' AND titolo = '%s'", email, titolo);

        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "Query fallita: %s", PQerrorMessage(conn));
            PQclear(res);
            return;
        }

        PQclear(res);
        json_decref(json_body);

        // Build the JSON response
        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{ \"message\": \"Libro rimosso dal carrello con successo\" }";
        send(newSocket, response, strlen(response), 0);
    }
}

void check_notifiche(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in check_notifiche\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Skip the 4 characters "\r\n\r\n"

        // Parse the JSON body of the request
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Extract the email from the JSON body
        const char *email = json_string_value(json_object_get(json_body, "email"));
        if (!email) {
            printf("Email mancante nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("Email ricevuta: %s\n", email);

        // Query to retrieve one notification for the user
        char query[512];
        snprintf(query, sizeof(query), "SELECT messaggio FROM notifiche WHERE email_utente = '%s' LIMIT 1", email);

        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Query fallita: %s", PQerrorMessage(conn));
            PQclear(res);
            json_decref(json_body);
            return;
        }

        // Prepare the response
        int buffer_size = 2048;
        char *response_buffer = malloc(buffer_size);
        if (!response_buffer) {
            fprintf(stderr, "Errore nell'allocazione del buffer\n");
            PQclear(res);
            json_decref(json_body);
            return;
        }

        int numRows = PQntuples(res);
        if (numRows > 0) {
            const char *messaggio = PQgetvalue(res, 0, 0);

            // Start of response
            snprintf(response_buffer, buffer_size, 
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "\r\n"
                     "{ \"success\": true, \"message\": \"Notifica inviata con successo\", \"notification\": \"%s\" }", 
                     messaggio);
           
        }

        // Send the response to the client
        if (send(newSocket, response_buffer, strlen(response_buffer), 0) < 0) {
            perror("Errore nell'invio della risposta al client");
        }

        free(response_buffer);
    }
}

void rimuovi_notifica(int newSocket, char *buffer, PGconn *conn) {
    fprintf(stderr, "Entrato in rimuovi_notifica\n");

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // Skip the 4 characters "\r\n\r\n"

        // Parse the JSON body of the request
        json_error_t error;
        json_t *json_body = json_loads(body, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Extract the email from the JSON body
        const char *notification = json_string_value(json_object_get(json_body, "notification"));
        if (!notification) {
            printf("notification mancante nel corpo JSON\n");
            json_decref(json_body);
            return;
        }

        printf("notification ricevuta: %s\n", notification);

        // Query to retrieve one notification for the user
        char query[512];
        snprintf(query, sizeof(query), "DELETE FROM notifiche WHERE messaggio = '%s'", notification);

        fprintf(stderr, "Query eseguita: %s\n", query);
        PGresult *res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Query fallita: %s", PQerrorMessage(conn));
            PQclear(res);
            json_decref(json_body);
            return;
        }

        const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "{ \"message\": \"Notifica rimossa correttamente\" }";
        printf("Notifica inviata al client\n");
        send(newSocket, response, strlen(response), 0);


        PQclear(res);
        json_decref(json_body);
    }

}
