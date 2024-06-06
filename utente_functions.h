#ifndef UTENTE_FUNCTIONS_H
#define UTENTE_FUNCTIONS_H

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


void handle_get_books(int newSocket, PGconn *conn, char *buffer);
void handle_get_carrello(int newSocket, char *buffer, PGconn *conn);
void handle_search_books(int newSocket, char *buffer, PGconn *conn);

void handle_add_to_cart(int newSocket, char *buffer, PGconn *conn);
void handle_remove_temporarily_from_cart(int newSocket, char *buffer, PGconn *conn);
void handle_restore_to_cart(int newSocket, char *buffer, PGconn *conn);
char** controlla_copie_disponibili(int newSocket, char *buffer, PGconn *conn, int *num_books);
char** controlla_libri_gia_presi_in_prestito(int newSocket, char *buffer, PGconn *conn, int *num_books);
void handle_crea_prestiti(int newSocket, char *buffer, PGconn *conn);
void get_limite_libri_per_utente(int newSocket, char *buffer, PGconn *conn);
void handle_get_prestiti_per_utente(int newSocket, char *buffer, PGconn *conn);
void restituisci_libro(int newSocket, char *buffer, PGconn *conn);
void rimuovi_definitivamente_carrello(int newSocket, char *buffer, PGconn *conn);
void check_notifiche(int newSocket, char *buffer, PGconn *conn);
void rimuovi_notifica(int newSocket, char *buffer, PGconn *conn);

#endif