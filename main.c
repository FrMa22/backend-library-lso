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

#define PORT 8082


void handle_options(int newSocket) {
    const char *response =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, GET, OPTIONS, PUT\r\n"  
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "\r\n";
    send(newSocket, response, strlen(response), 0);
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

void print_books_found(PGresult *res) {
    int num_rows = PQntuples(res);
    printf("Libri trovati:\n");
    for (int i = 0; i < num_rows; i++) {
        printf("Titolo: %s, Autore: %s, Genere: %s, Copie Totali: %s, Copie in Prestito: %s\n",
               PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2),
               PQgetvalue(res, i, 3), PQgetvalue(res, i, 4));
    }
}

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

        // Esegui l'inserimento nel database
        char query[512];
        snprintf(query, sizeof(query), "INSERT INTO carrello (email, titolo) VALUES ('%s', '%s')", email, bookTitle);

        PGresult *res = PQexec(conn, query);

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

        // Estrai l'email dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_body, "email"));
        if (!email) {
            printf("Email mancante nel corpo JSON\n");
            json_decref(json_body);
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
                return;
            }

            if(num_books>0 && num_books_already_borrowed_in_cart==0){
                // Crea l'inizio della risposta
                snprintf(buffer, buffer_size, 
                    "HTTP/1.1 202 \r\n"
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
                    "HTTP/1.1 202 \r\n"
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
                    return;
                }

                // Crea l'inizio della risposta
                snprintf(buffer, buffer_size, 
                    "HTTP/1.1 202 \r\n"
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
        }
        json_decref(json_body);
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
void handle_registration(int newSocket, char *buffer, PGconn *conn) {
    printf("Registrando nuovo utente\n");

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

        // Estrai i campi dall'oggetto JSON
        const char *email = json_string_value(json_object_get(json_body_req, "email"));
        const char *password = json_string_value(json_object_get(json_body_req, "password"));
        if (!email || !password) {
            printf("Email o password mancanti nel corpo JSON\n");
            json_decref(json_body_req);
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
            send(newSocket, response, strlen(response), 0);

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
        send(newSocket, response, strlen(response), 0);

        printf("Registrazione avvenuta con successo per l'utente con email: %s\n", email);

        // Libera la memoria del corpo JSON della richiesta
        json_decref(json_body_req);
    }
}


 
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
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "Nessun prestito scaduto trovato";
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
 
 
 
 

void invia_messaggio(int newSocket, PGconn *conn,const char *titolo,const char *email,const char *data_scadenza) {
    printf("Entrato in invia_messaggio\n");
    // Recupero informazioni  libro dal database e invio al client
   
    // Stampa i valori estratti (opzionale, per debug)
        printf("Titolo: %s\n", titolo);
        printf("Email: %s\n", email);
        printf("Scadenza: %s\n", data_scadenza);

     // Costruisce il messaggio per la notifica
    char messaggio[300];
    snprintf(messaggio, sizeof(messaggio), "Attenzione! Il prestito per il libro %s è scaduto il giorno %s.Si prega di restituire il libro.", titolo, data_scadenza);
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
        close(newSocket);
        free(data);
        return NULL;
    }

    char buffer[2048] = {0};

    int valread = recv(newSocket, buffer, sizeof(buffer) - 1, 0);
    if (valread <= 0) {
        perror("Receive failed or connection closed\n");
        close(newSocket);
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
    } else {
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
    }else if (strncmp(buffer, "POST /messaggio", 15) == 0) {  // Gestisce l'invio di un messaggio
     // Cerca l'inizio del corpo della richiesta
    char *body_start = strstr(buffer, "\r\n\r\n");
    if (body_start) {
        body_start += 4; // Avanza oltre la sequenza "\r\n\r\n"
        // Parsing del corpo della richiesta JSON
        json_error_t error;
        json_t *json_body = json_loads(body_start, 0, &error);
        if (!json_body) {
            printf("Errore nel parsing del corpo JSON: %s\n", error.text);
            return;
        }

        // Estrai i valori dal JSON
        const char *titolo = json_string_value(json_object_get(json_body, "titolo"));
        const char *email = json_string_value(json_object_get(json_body, "email"));
        const char *scadenza = json_string_value(json_object_get(json_body, "scadenza"));

        // Stampa i valori estratti (opzionale, per debug)
        printf("Titolo: %s\n", titolo);
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
            }
            char titolo[256];
            strcpy(titolo, titolo_start);
            printf("Titolo: %s\n", titolo);
            handle_get_info(newSocket, conn, titolo);
        }
    }
        
        
        
     
        

    close(newSocket);
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

    // Creazione del socket del server
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

        thread_data_t *data = malloc(sizeof(thread_data_t));
        if (!data) {
            perror("Failed to allocate memory\n");
            close(newSocket);
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
            close(newSocket);
            free(data);
            continue;
        }

        pthread_detach(client_thread);
    }

    close(serverSocket);

    return 0;
}

//    gcc -o main main.c -I/usr/include/postgresql -L/usr/lib/x86_64-linux-gnu -ljansson -lpq

