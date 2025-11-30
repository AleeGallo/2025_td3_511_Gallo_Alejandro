#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/**
 * Envia un comando por /dev/egb y espera hasta recibir una linea
 * que contenga "PONG" o "pong". Mientras tanto, muestra todo lo recibido.
 * Lo usamos para PING.
 */
static int enviar_y_esperar_pong(int fd, const char *cmd) {
    char buf[256];
    int n;

    if (write(fd, cmd, strlen(cmd)) < 0) {
        perror("write");
        return -1;
    }

    printf(">> Enviado: %s\n", cmd);
    printf("... esperando PONG ...\n");

    for (;;) {
        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            return -1;
        }
        if (n == 0) {
            printf("read devolvio 0 (EOF?)\n");
            break;
        }

        buf[n] = '\0';
        printf("<< Recibido: %s\n", buf);

        if (strstr(buf, "PONG") != NULL || strstr(buf, "pong") != NULL) {
            printf("PONG detectado, volviendo al menu.\n");
            break;
        }
    }

    return 0;
}

/**
 * Envia un comando por /dev/egb y espera hasta recibir una linea
 * que contenga "ACK#" o "ack#". Mientras tanto, muestra todo lo recibido.
 * Lo usamos para RUN, STOP y los TUNE.
 */
static int enviar_y_esperar_ack(int fd, const char *cmd) {
    char buf[256];
    int n;

    if (write(fd, cmd, strlen(cmd)) < 0) {
        perror("write");
        return -1;
    }

    printf(">> Enviado: %s\n", cmd);
    printf("... esperando ACK# ...\n");

    for (;;) {
        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            return -1;
        }
        if (n == 0) {
            printf("read devolvio 0 (EOF?)\n");
            break;
        }

        buf[n] = '\0';
        printf("<< Recibido: %s\n", buf);

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

        printf("\n===== MENU EGB =====\n");
        printf("1) PING (espera PONG)\n");
        printf("2) RUN v t (floats, espera ACK#)\n");
        printf("3) STOP (espera ACK#)\n");
        printf("4) TUNE   (6 floats PID, espera ACK#)\n");
        printf("5) TUNE_L (3 floats PID, espera ACK#)\n");
        printf("6) TUNE_R (3 floats PID, espera ACK#)\n");
        printf("7) Salir\n");
        printf("Opcion: ");

        if (scanf("%d", &opcion) != 1) {
            printf("Entrada inválida\n");
            while ((c = getchar()) != '\n' && c != EOF) {}
            continue;
        }

        while ((c = getchar()) != '\n' && c != EOF) {}

        if (opcion == 1) {
            /* PING: manda PING# y espera PONG */
            snprintf(linea, sizeof(linea), "PING#");
            enviar_y_esperar_pong(fd, linea);

        } else if (opcion == 2) {
            /* RUN v t como floats, espera ACK# */
            float v, t;

            printf("Velocidad (float): ");
            if (scanf("%f", &v) != 1) goto invalido;

            printf("Tiempo (float): ");
            if (scanf("%f", &t) != 1) goto invalido;

            while ((c = getchar()) != '\n' && c != EOF) {}

            /* RUN v t# */
            snprintf(linea, sizeof(linea), "RUN %.2f %.2f#", v, t);
            enviar_y_esperar_ack(fd, linea);
            continue;

        } else if (opcion == 3) {
            /* STOP# */
            snprintf(linea, sizeof(linea), "STOP#");
            enviar_y_esperar_ack(fd, linea);

        } else if (opcion == 4) {
            /* TUNE: PID completo izquierda + derecha, floats, espera ACK#
               TUNE KpL KiL KdL KpR KiR KdR#
            */
            float KpL, KiL, KdL, KpR, KiR, KdR;

            printf("Kp_left: ");  if (scanf("%f", &KpL) != 1) goto invalido;
            printf("Ki_left: ");  if (scanf("%f", &KiL) != 1) goto invalido;
            printf("Kd_left: ");  if (scanf("%f", &KdL) != 1) goto invalido;
            printf("Kp_right: "); if (scanf("%f", &KpR) != 1) goto invalido;
            printf("Ki_right: "); if (scanf("%f", &KiR) != 1) goto invalido;
            printf("Kd_right: "); if (scanf("%f", &KdR) != 1) goto invalido;

            while ((c = getchar()) != '\n' && c != EOF) {}

            snprintf(linea, sizeof(linea),
                     "TUNE %.2f %.2f %.2f %.2f %.2f %.2f#",
                     KpL, KiL, KdL, KpR, KiR, KdR);
            enviar_y_esperar_ack(fd, linea);
            continue;

        } else if (opcion == 5) {
            /* TUNE_L KpL KiL KdL# */
            float KpL, KiL, KdL;

            printf("Kp_left: "); if (scanf("%f", &KpL) != 1) goto invalido;
            printf("Ki_left: "); if (scanf("%f", &KiL) != 1) goto invalido;
            printf("Kd_left: "); if (scanf("%f", &KdL) != 1) goto invalido;

            while ((c = getchar()) != '\n' && c != EOF) {}

            snprintf(linea, sizeof(linea),
                     "TUNE_L %.2f %.2f %.2f#",
                     KpL, KiL, KdL);
            enviar_y_esperar_ack(fd, linea);
            continue;

        } else if (opcion == 6) {
            /* TUNE_R KpR KiR KdR# */
            float KpR, KiR, KdR;

            printf("Kp_right: "); if (scanf("%f", &KpR) != 1) goto invalido;
            printf("Ki_right: "); if (scanf("%f", &KiR) != 1) goto invalido;
            printf("Kd_right: "); if (scanf("%f", &KdR) != 1) goto invalido;

            while ((c = getchar()) != '\n' && c != EOF) {}

            snprintf(linea, sizeof(linea),
                     "TUNE_R %.2f %.2f %.2f#",
                     KpR, KiR, KdR);
            enviar_y_esperar_ack(fd, linea);
            continue;

        } else if (opcion == 7) {
            printf("Saliendo...\n");
            break;

        } else {
            printf("Opción inválida.\n");
        }

        continue;

invalido:
        printf("Entrada inválida. Volviendo al menú.\n");
        while ((c = getchar()) != '\n' && c != EOF) {}
        continue;
    }

    close(fd);
    return 0;
}
