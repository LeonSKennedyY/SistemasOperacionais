#ifndef BANCO_H
#define BANCO_H

#include <pthread.h>

#define MAX_REGISTROS 100
#define MAX_NOME 50
#define MAX_LINHA 256

#define REQ_FIFO "/tmp/bd_req_fifo"
#define RESP_FIFO "/tmp/bd_resp_fifo"

typedef struct {
    int id;
    char nome[MAX_NOME];
    int ativo;
} Registro;

typedef struct {
    Registro registros[MAX_REGISTROS];
    int quantidade;
    pthread_mutex_t mutex;
} Banco;

/* Inicialização/finalização */
void banco_init(Banco *banco);
void banco_destroy(Banco *banco);

/* Operações do banco */
int banco_insert(Banco *banco, int id, const char *nome, char *resposta, int tam_resposta);
int banco_select(Banco *banco, int id, char *resposta, int tam_resposta);
int banco_update(Banco *banco, int id, const char *nome, char *resposta, int tam_resposta);
int banco_delete(Banco *banco, int id, char *resposta, int tam_resposta);

#endif