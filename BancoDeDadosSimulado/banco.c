#include "banco.h"
#include <stdio.h>
#include <string.h>

void banco_init(Banco *banco) {
    banco->quantidade = 0;
    pthread_mutex_init(&banco->mutex, NULL);

    for (int i = 0; i < MAX_REGISTROS; i++) {
        banco->registros[i].ativo = 0;
        banco->registros[i].id = 0;
        banco->registros[i].nome[0] = '\0';
    }
}

void banco_destroy(Banco *banco) {
    pthread_mutex_destroy(&banco->mutex);
}

int banco_insert(Banco *banco, int id, const char *nome, char *resposta, int tam_resposta) {
    pthread_mutex_lock(&banco->mutex);

    for (int i = 0; i < MAX_REGISTROS; i++) {
        if (banco->registros[i].ativo && banco->registros[i].id == id) {
            snprintf(resposta, tam_resposta, "ERRO: id=%d ja existe", id);
            pthread_mutex_unlock(&banco->mutex);
            return 0;
        }
    }

    for (int i = 0; i < MAX_REGISTROS; i++) {
        if (!banco->registros[i].ativo) {
            banco->registros[i].ativo = 1;
            banco->registros[i].id = id;
            snprintf(banco->registros[i].nome, MAX_NOME, "%s", nome);
            banco->quantidade++;

            snprintf(resposta, tam_resposta, "OK: INSERT id=%d nome=%s", id, nome);
            pthread_mutex_unlock(&banco->mutex);
            return 1;
        }
    }

    snprintf(resposta, tam_resposta, "ERRO: banco cheio");
    pthread_mutex_unlock(&banco->mutex);
    return 0;
}

int banco_select(Banco *banco, int id, char *resposta, int tam_resposta) {
    pthread_mutex_lock(&banco->mutex);

    for (int i = 0; i < MAX_REGISTROS; i++) {
        if (banco->registros[i].ativo && banco->registros[i].id == id) {
            snprintf(resposta, tam_resposta, "OK: SELECT id=%d nome=%s",
                     banco->registros[i].id, banco->registros[i].nome);
            pthread_mutex_unlock(&banco->mutex);
            return 1;
        }
    }

    snprintf(resposta, tam_resposta, "ERRO: id=%d nao encontrado", id);
    pthread_mutex_unlock(&banco->mutex);
    return 0;
}

int banco_update(Banco *banco, int id, const char *nome, char *resposta, int tam_resposta) {
    pthread_mutex_lock(&banco->mutex);

    for (int i = 0; i < MAX_REGISTROS; i++) {
        if (banco->registros[i].ativo && banco->registros[i].id == id) {
            snprintf(banco->registros[i].nome, MAX_NOME, "%s", nome);
            snprintf(resposta, tam_resposta, "OK: UPDATE id=%d novo_nome=%s", id, nome);
            pthread_mutex_unlock(&banco->mutex);
            return 1;
        }
    }

    snprintf(resposta, tam_resposta, "ERRO: id=%d nao encontrado", id);
    pthread_mutex_unlock(&banco->mutex);
    return 0;
}

int banco_delete(Banco *banco, int id, char *resposta, int tam_resposta) {
    pthread_mutex_lock(&banco->mutex);

    for (int i = 0; i < MAX_REGISTROS; i++) {
        if (banco->registros[i].ativo && banco->registros[i].id == id) {
            banco->registros[i].ativo = 0;
            banco->registros[i].id = 0;
            banco->registros[i].nome[0] = '\0';
            banco->quantidade--;

            snprintf(resposta, tam_resposta, "OK: DELETE id=%d", id);
            pthread_mutex_unlock(&banco->mutex);
            return 1;
        }
    }

    snprintf(resposta, tam_resposta, "ERRO: id=%d nao encontrado", id);
    pthread_mutex_unlock(&banco->mutex);
    return 0;
}