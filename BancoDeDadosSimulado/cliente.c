#include "banco.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void mostrar_ajuda(void) {
    printf("Comandos disponiveis:\n");
    printf("  INSERT <id> <nome>\n");
    printf("  SELECT <id>\n");
    printf("  UPDATE <id> <novo_nome>\n");
    printf("  DELETE <id>\n");
    printf("  EXIT\n");
    printf("  HELP\n");
}

int main(void) {
    int req_fd = open(REQ_FIFO, O_WRONLY);
    if (req_fd == -1) {
        perror("Erro ao abrir FIFO de requisicoes");
        fprintf(stderr, "O servidor esta em execucao?\n");
        return 1;
    }

    int resp_fd = open(RESP_FIFO, O_RDONLY);
    if (resp_fd == -1) {
        perror("Erro ao abrir FIFO de respostas");
        close(req_fd);
        return 1;
    }

    FILE *req_stream = fdopen(req_fd, "w");
    FILE *resp_stream = fdopen(resp_fd, "r");

    if (!req_stream || !resp_stream) {
        perror("Erro ao associar stream");
        if (req_stream) fclose(req_stream); else close(req_fd);
        if (resp_stream) fclose(resp_stream); else close(resp_fd);
        return 1;
    }

    printf("Cliente conectado ao servidor.\n");
    mostrar_ajuda();

    char linha[MAX_LINHA];
    char resposta[MAX_LINHA];

    while (1) {
        printf("\nDigite um comando> ");
        fflush(stdout);

        if (!fgets(linha, sizeof(linha), stdin)) {
            break;
        }

        linha[strcspn(linha, "\n")] = '\0';

        if (strlen(linha) == 0) {
            continue;
        }

        if (strcmp(linha, "HELP") == 0) {
            mostrar_ajuda();
            continue;
        }

        fprintf(req_stream, "%s\n", linha);
        fflush(req_stream);

        if (fgets(resposta, sizeof(resposta), resp_stream) == NULL) {
            printf("Conexao com servidor encerrada.\n");
            break;
        }

        printf("Resposta: %s", resposta);

        if (strcmp(linha, "EXIT") == 0) {
            break;
        }
    }

    fclose(req_stream);
    fclose(resp_stream);
    return 0;
}