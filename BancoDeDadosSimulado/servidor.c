#include "banco.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_THREADS 4
#define TAM_FILA 100

typedef struct {
    Banco *banco;
    int resp_fd;
    pthread_mutex_t *resp_mutex;
    char linha[MAX_LINHA];
} Tarefa;

typedef struct {
    Tarefa tarefas[TAM_FILA];
    int inicio;
    int fim;
    int quantidade;
    int encerrando;

    pthread_mutex_t mutex;
    pthread_cond_t cond_nao_vazia;
    pthread_cond_t cond_nao_cheia;
} FilaTarefas;

typedef struct {
    Banco *banco;
    int resp_fd;
    pthread_mutex_t *resp_mutex;
    FilaTarefas *fila;
} WorkerArgs;

static void trim_newline(char *s) {
    s[strcspn(s, "\n")] = '\0';
}

static void enviar_resposta(int resp_fd, pthread_mutex_t *resp_mutex, const char *msg) {
    pthread_mutex_lock(resp_mutex);
    dprintf(resp_fd, "%s\n", msg);
    pthread_mutex_unlock(resp_mutex);
}

static void fila_init(FilaTarefas *fila) {
    fila->inicio = 0;
    fila->fim = 0;
    fila->quantidade = 0;
    fila->encerrando = 0;

    pthread_mutex_init(&fila->mutex, NULL);
    pthread_cond_init(&fila->cond_nao_vazia, NULL);
    pthread_cond_init(&fila->cond_nao_cheia, NULL);
}

static void fila_destroy(FilaTarefas *fila) {
    pthread_mutex_destroy(&fila->mutex);
    pthread_cond_destroy(&fila->cond_nao_vazia);
    pthread_cond_destroy(&fila->cond_nao_cheia);
}

static int fila_inserir(FilaTarefas *fila, const Tarefa *tarefa) {
    pthread_mutex_lock(&fila->mutex);

    while (fila->quantidade == TAM_FILA && !fila->encerrando) {
        pthread_cond_wait(&fila->cond_nao_cheia, &fila->mutex);
    }

    if (fila->encerrando) {
        pthread_mutex_unlock(&fila->mutex);
        return 0;
    }

    fila->tarefas[fila->fim] = *tarefa;
    fila->fim = (fila->fim + 1) % TAM_FILA;
    fila->quantidade++;

    pthread_cond_signal(&fila->cond_nao_vazia);
    pthread_mutex_unlock(&fila->mutex);
    return 1;
}

static int fila_remover(FilaTarefas *fila, Tarefa *tarefa) {
    pthread_mutex_lock(&fila->mutex);

    while (fila->quantidade == 0 && !fila->encerrando) {
        pthread_cond_wait(&fila->cond_nao_vazia, &fila->mutex);
    }

    if (fila->quantidade == 0 && fila->encerrando) {
        pthread_mutex_unlock(&fila->mutex);
        return 0;
    }

    *tarefa = fila->tarefas[fila->inicio];
    fila->inicio = (fila->inicio + 1) % TAM_FILA;
    fila->quantidade--;

    pthread_cond_signal(&fila->cond_nao_cheia);
    pthread_mutex_unlock(&fila->mutex);
    return 1;
}

static void processar_comando(Tarefa *tarefa) {
    char comando[20] = {0};
    int id = 0;
    char nome[MAX_NOME] = {0};
    char resposta[MAX_LINHA] = {0};

    trim_newline(tarefa->linha);

    if (sscanf(tarefa->linha, "%19s", comando) != 1) {
        enviar_resposta(tarefa->resp_fd, tarefa->resp_mutex, "ERRO: requisicao vazia");
        return;
    }

    if (strcasecmp(comando, "INSERT") == 0) {
        if (sscanf(tarefa->linha, "%19s %d %49[^\n]", comando, &id, nome) == 3) {
            banco_insert(tarefa->banco, id, nome, resposta, sizeof(resposta));
        } else {
            snprintf(resposta, sizeof(resposta), "ERRO: uso correto -> INSERT <id> <nome>");
        }
    } else if (strcasecmp(comando, "SELECT") == 0) {
        if (sscanf(tarefa->linha, "%19s %d", comando, &id) == 2) {
            banco_select(tarefa->banco, id, resposta, sizeof(resposta));
        } else {
            snprintf(resposta, sizeof(resposta), "ERRO: uso correto -> SELECT <id>");
        }
    } else if (strcasecmp(comando, "UPDATE") == 0) {
        if (sscanf(tarefa->linha, "%19s %d %49[^\n]", comando, &id, nome) == 3) {
            banco_update(tarefa->banco, id, nome, resposta, sizeof(resposta));
        } else {
            snprintf(resposta, sizeof(resposta), "ERRO: uso correto -> UPDATE <id> <novo_nome>");
        }
    } else if (strcasecmp(comando, "DELETE") == 0) {
        if (sscanf(tarefa->linha, "%19s %d", comando, &id) == 2) {
            banco_delete(tarefa->banco, id, resposta, sizeof(resposta));
        } else {
            snprintf(resposta, sizeof(resposta), "ERRO: uso correto -> DELETE <id>");
        }
    } else {
        snprintf(resposta, sizeof(resposta), "ERRO: comando desconhecido");
    }

    enviar_resposta(tarefa->resp_fd, tarefa->resp_mutex, resposta);
}

static void *worker_thread(void *arg) {
    WorkerArgs *wargs = (WorkerArgs *)arg;
    Tarefa tarefa;

    while (fila_remover(wargs->fila, &tarefa)) {
        processar_comando(&tarefa);
    }

    return NULL;
}

int main(void) {
    Banco banco;
    banco_init(&banco);

    pthread_mutex_t resp_mutex;
    pthread_mutex_init(&resp_mutex, NULL);

    FilaTarefas fila;
    fila_init(&fila);

    pthread_t workers[NUM_THREADS];
    WorkerArgs wargs;

    wargs.banco = &banco;
    wargs.resp_fd = -1;
    wargs.resp_mutex = &resp_mutex;
    wargs.fila = &fila;

    unlink(REQ_FIFO);
    unlink(RESP_FIFO);

    if (mkfifo(REQ_FIFO, 0666) == -1 && errno != EEXIST) {
        perror("Erro ao criar FIFO de requisicoes");
        pthread_mutex_destroy(&resp_mutex);
        fila_destroy(&fila);
        banco_destroy(&banco);
        return 1;
    }

    if (mkfifo(RESP_FIFO, 0666) == -1 && errno != EEXIST) {
        perror("Erro ao criar FIFO de respostas");
        unlink(REQ_FIFO);
        pthread_mutex_destroy(&resp_mutex);
        fila_destroy(&fila);
        banco_destroy(&banco);
        return 1;
    }

    int req_fd = open(REQ_FIFO, O_RDONLY);
    if (req_fd == -1) {
        perror("Erro ao abrir FIFO de requisicoes");
        unlink(REQ_FIFO);
        unlink(RESP_FIFO);
        pthread_mutex_destroy(&resp_mutex);
        fila_destroy(&fila);
        banco_destroy(&banco);
        return 1;
    }

    int req_dummy_fd = open(REQ_FIFO, O_WRONLY);
    if (req_dummy_fd == -1) {
        perror("Erro ao abrir FIFO dummy de requisicoes");
        close(req_fd);
        unlink(REQ_FIFO);
        unlink(RESP_FIFO);
        pthread_mutex_destroy(&resp_mutex);
        fila_destroy(&fila);
        banco_destroy(&banco);
        return 1;
    }

    int resp_fd = open(RESP_FIFO, O_RDWR);
    if (resp_fd == -1) {
        perror("Erro ao abrir FIFO de respostas");
        close(req_fd);
        close(req_dummy_fd);
        unlink(REQ_FIFO);
        unlink(RESP_FIFO);
        pthread_mutex_destroy(&resp_mutex);
        fila_destroy(&fila);
        banco_destroy(&banco);
        return 1;
    }

    FILE *req_stream = fdopen(req_fd, "r");
    if (!req_stream) {
        perror("Erro ao associar stream ao FIFO");
        close(req_fd);
        close(req_dummy_fd);
        close(resp_fd);
        unlink(REQ_FIFO);
        unlink(RESP_FIFO);
        pthread_mutex_destroy(&resp_mutex);
        fila_destroy(&fila);
        banco_destroy(&banco);
        return 1;
    }

    wargs.resp_fd = resp_fd;

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, &wargs) != 0) {
            perror("Erro ao criar thread do pool");
            fila.encerrando = 1;
            pthread_cond_broadcast(&fila.cond_nao_vazia);
            pthread_cond_broadcast(&fila.cond_nao_cheia);

            for (int j = 0; j < i; j++) {
                pthread_join(workers[j], NULL);
            }

            fclose(req_stream);
            close(req_dummy_fd);
            close(resp_fd);
            unlink(REQ_FIFO);
            unlink(RESP_FIFO);
            pthread_mutex_destroy(&resp_mutex);
            fila_destroy(&fila);
            banco_destroy(&banco);
            return 1;
        }
    }

    printf("Servidor iniciado.\n");
    printf("FIFO requisicoes: %s\n", REQ_FIFO);
    printf("FIFO respostas  : %s\n", RESP_FIFO);
    printf("Pool de threads : %d\n", NUM_THREADS);
    printf("Aguardando comandos...\n");

    char linha[MAX_LINHA];

    while (fgets(linha, sizeof(linha), req_stream) != NULL) {
        trim_newline(linha);

        if (strcmp(linha, "EXIT") == 0) {
            enviar_resposta(resp_fd, &resp_mutex, "OK: servidor encerrando");
            break;
        }

        Tarefa tarefa;
        tarefa.banco = &banco;
        tarefa.resp_fd = resp_fd;
        tarefa.resp_mutex = &resp_mutex;
        snprintf(tarefa.linha, sizeof(tarefa.linha), "%s", linha);

        if (!fila_inserir(&fila, &tarefa)) {
            enviar_resposta(resp_fd, &resp_mutex, "ERRO: nao foi possivel enfileirar requisicao");
        }
    }

    pthread_mutex_lock(&fila.mutex);
    fila.encerrando = 1;
    pthread_cond_broadcast(&fila.cond_nao_vazia);
    pthread_cond_broadcast(&fila.cond_nao_cheia);
    pthread_mutex_unlock(&fila.mutex);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(workers[i], NULL);
    }

    fclose(req_stream);
    close(req_dummy_fd);
    close(resp_fd);

    fila_destroy(&fila);
    pthread_mutex_destroy(&resp_mutex);
    banco_destroy(&banco);

    unlink(REQ_FIFO);
    unlink(RESP_FIFO);

    printf("Servidor finalizado.\n");
    return 0;
}