#ifndef LIBRAIO_FUNCTIONS_H
#define LIBRAIO_FUNCTIONS_H

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

bool string_to_bool(const char *str);
void handle_get_libri(int newSocket, PGconn *conn);
void handle_get_scaduti(int newSocket, PGconn *conn);
void get_limite_libri(int newSocket, PGconn *conn);
void update_limite_libri(int newSocket, PGconn *conn, char *buffer);
void handle_get_info(int newSocket, PGconn *conn,const char *titolo);
void invia_messaggio(int newSocket, PGconn *conn,const char *titolo,const char *email,const char *data_scadenza);


#endif