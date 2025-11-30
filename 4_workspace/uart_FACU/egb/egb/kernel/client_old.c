#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/**
 * Envia un comando por /dev/egb y lee UNA sola linea de respuesta.
 * No espera ACK#, simplemente muestra lo que llegue y vuelve al menu.
 * Lo usamos para PING y STOP.
 */
static int enviar_y_recibir_simple(int fd, const char *cmd) {
    char buf[256];
    int n;

    if (write(fd, cmd, strlen(cmd)) < 0) {
        perror("write");
        return -1;
    }

    printf(">> Enviado: %s", cmd);
    printf("... esperando una linea de respuesta ...\n");

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        return -1;
    }
    if (n == 0) {
        printf("read devolvio 0 (EOF?), sin datos.\n");
        return 0;
    }

    buf[n] = '\0';
    printf("<< Respuesta: %s\n", buf);
    return 0;
}

/**
 * Envia un comando por /dev/egb y espera hasta recibir una linea
 * que contenga "ACK#" o "ack#". Mientras tanto, muestra todo lo recibido.
 * Lo usamos solo para RUN.
 */
static int enviar_y_esperar_ack(int fd, const char *cmd) {
    char buf[256];
    int n;

    if (write(fd, cmd, strlen(cmd)) < 0) {
        perror("write");
        return -1;
    }

    printf(">> Enviado: %s", cmd);
    printf("... esperando ACK# ...\n");

    for (;;) {
        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            return -1;
        }
        if (n == 0) {
            printf("read devolvio 0 (EOF?), saliendo del loop de ACK.\n");
            break;
        }

        buf[n] = '\0';
        printf("<< Recibido: %s\n", buf);

        /* Buscar "ACK#" o "ack#" en la linea */
        if (strstr(buf, "ACK#") != NULL || strstr(buf, "ack#") != NULL) {
            printf("ACK# detectado, volviendo al menu.\n");
            break;
        }
    }

    return 0;
}

int main(void) {
    int fd;
    int opcion;
    char linea[256];

    fd = open("/dev/egb", O_RDWR);
    if (fd < 0) {
        perror("open /dev/egb");
        return 1;
    }

    while (1) {
        int c;

        printf("===== MENU EGB =====\n");
        printf("1) PING (lectura simple)\n");
        printf("2) RUN v t (enteros, espera ACK#)\n");
        printf("3) STOP (lectura simple)\n");
        printf("4) Salir\n");
        printf("Opcion: ");

        if (scanf("%d", &opcion) != 1) {
            fprintf(stderr, "Entrada invalida\n");
            /* limpiar stdin */
            while ((c = getchar()) != '\n' && c != EOF) { }
            continue;
        }

        /* limpiar \n pendiente de scanf */
        while ((c = getchar()) != '\n' && c != EOF) { }

        if (opcion == 1) {
            /* PING: manda PING\n y lee UNA sola linea */
            snprintf(linea, sizeof(linea), "PING\n");
            enviar_y_recibir_simple(fd, linea);

        } else if (opcion == 2) {
            /* RUN v t como ENTEROS, espera ACK# */
            int v, t;

            printf("Ingrese velocidad (entero, por ejemplo 2): ");
            if (scanf("%d", &v) != 1) {
                printf("Velocidad invalida\n");
                while ((c = getchar()) != '\n' && c != EOF) { }
                continue;
            }

            printf("Ingrese tiempo (entero, por ejemplo 5): ");
            if (scanf("%d", &t) != 1) {
                printf("Tiempo invalido\n");
                while ((c = getchar()) != '\n' && c != EOF) { }
                continue;
            }

            /* limpiar \n pendiente de scanf */
            while ((c = getchar()) != '\n' && c != EOF) { }

            /* Formato EXACTO: RUN 2 5\n */
            snprintf(linea, sizeof(linea), "RUN %d %d\n", v, t);
            enviar_y_esperar_ack(fd, linea);

        } else if (opcion == 3) {
            /* STOP: lectura simple */
            snprintf(linea, sizeof(linea), "STOP\n");
            enviar_y_recibir_simple(fd, linea);

        } else if (opcion == 4) {
            printf("Saliendo...\n");
            break;

        } else {
            printf("Opcion invalida\n");
        }
    }

    close(fd);
    return 0;
}
