#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/sem.h>
#include "myassert.h"
#include "sys/types.h"
#include "sys/stat.h"
#include <fcntl.h>

#include "master_client.h"
#include "master_worker.h"

#define MAX_WORKERS 10

/************************************************************************
 * Données persistantes d'un master
 ************************************************************************/

// on peut ici définir une structure stockant tout ce dont le master
// a besoin

/* structure pour échanger avec les workers */
struct result {
    int n;          // le nombre testé par le worker
    int isPrime;    // 1 si le nombre est premier, 0 sinon
};

// structure pour le master
typedef struct {
    // 1. État du calcul
    int M;              // dernier nombre testé
    int nbPrimes;       // compteur de nombres premiers trouvés
    int highestPrime;   // plus grand nombre premier trouvé
    
    // 2. Communications Worker (pipes anonymes)
    int pipeMW[2];      // master -> worker (écriture dans pipeMW[1], lecture dans pipeMW[0] par le worker)
    int pipeWM[2];      // worker -> master (lecture dans pipeWM[0], écriture dans pipeWM[1] par le worker)
    
    // 3. Communications Client (FIFOs) - Noms des fichiers
    const char *fifo1_name; // FIFO client -> master
    const char *fifo2_name; // FIFO master -> client
    
    // 4. Communications Client (Sémaphores IPC)
    int sem_mutex;      // Sémaphore mutex
    int sem_sync;       // Sémaphore de synchronisation
} master_t;

static void init_ipc(master_t *master)
{
    // Définition des clés pour les sémaphores
    key_t key_mutex = MUTEX_KEY;
    key_t key_sync  = SYNC_KEY;

    // Création du sémaphore mutex
    master->sem_mutex = semget(key_mutex, 1, IPC_CREAT | 0641);
    myassert(master->sem_mutex != -1, "semget mutex");

    // Création du sémaphore de synchronisation
    master->sem_sync = semget(key_sync, 1, IPC_CREAT | 0641);
    myassert(master->sem_sync != -1, "semget sync");

    // Initialisation du mutex à 1
    int ret = semctl(master->sem_mutex, 0, SETVAL, 1);
    myassert(ret != -1, "semctl mutex SETVAL");

    // Initialisation du sémaphore de synchronisation à 0
    ret = semctl(master->sem_sync, 0, SETVAL, 0);
    myassert(ret != -1, "semctl sync SETVAL");

    // Noms des FIFO
    master->fifo1_name = FIFO_CLIENT_MASTER;
    master->fifo2_name = FIFO_MASTER_CLIENT;

    // Création des FIFO
    ret = mkfifo(master->fifo1_name, 0641);
    myassert(ret != -1, "mkfifo client->master");

    ret = mkfifo(master->fifo2_name, 0641);
    myassert(ret != -1, "mkfifo master->client");
}

/* suppression des ipc en fin de programme */
static void clean_ipc(master_t *master)
{
    // Suppression des FIFO
    unlink(master->fifo1_name);
    unlink(master->fifo2_name);

    // Suppression des sémaphores
    semctl(master->sem_mutex, 0, IPC_RMID);
    semctl(master->sem_sync, 0, IPC_RMID);
}

/* attente complète des workers avant sortie */
static void wait_for_all_children(void)
{
    int status;
    while (wait(&status) > 0)
        ;
}

/**
 * Arrêt demandé par le client
 * @param ctx : Le contexte du master contenant les pipes
 * @param fdWrite : FD de la FIFO master->client (ouvert)
 * @param fdRead : FD de la FIFO client->master (ouvert)
 */
void client_stop(master_t *master, int fdWrite, int fdRead)
{
    int stop = -1; // valeur spéciale pour indiquer l'arrêt des workers

    /* envoi du signal d'arrêt aux workers via le pipe master->worker */
    if (write(master->pipeMW[1], &stop, sizeof(int)) != sizeof(int)) {
        perror("write stop to worker");
    }

    // Fermeture des extrémités inutilisées des pipes
    close(master->pipeMW[1]); // fermeture de l'écriture vers worker
    close(master->pipeWM[0]); // fermeture de la lecture depuis worker

    // Attente que tous les workers aient terminé
    wait_for_all_children();

    // Envoi d'un accusé de réception au client pour confirmer l'arrêt
    int ack = 1;
    if (write(fdWrite, &ack, sizeof(int)) != sizeof(int)) {
        perror("write ack to client");
    }

    // Fermeture des FIFO
    close(fdRead);
    close(fdWrite);
}

/**
 * Calcul demandé par le client jusqu'à la borne N
 * @param ctx : Le contexte du master (contient M, pipes, et résultats cumulés)
 * @param N : La borne supérieure demandée par le client
 * @param fdWrite : FD de la FIFO master->client (pour envoyer le résultat de N)
 */
void client_compute_prime(master_t *master, int N, int fdWrite)
{
    // Boucle sur tous les nombres à tester, de M+2 jusqu'à N-1
    // Note : Le 2 (premier nombre premier) est géré implicitement par le premier worker
    for (int i = master->M + 2; i < N; i++) {
        // Envoi du nombre i au worker via le pipe master->worker
        myassert(write(master->pipeMW[1], &i, sizeof(int)) == sizeof(int),
                 "write number to worker");

        // Lecture du résultat du worker
        struct result m;
        ssize_t ret = read(master->pipeWM[0], &m, sizeof(struct result));
        myassert(ret == sizeof(struct result), "read from worker failed");

        // Mise à jour des statistiques
        if (m.isPrime) {
            master->nbPrimes++;
            if (m.n > master->highestPrime)
                master->highestPrime = m.n;
        }
    }

    // Envoyer le nombre N lui-même au worker
    myassert(write(master->pipeMW[1], &N, sizeof(int)) == sizeof(int),
             "write number N to worker");

    // Lire le résultat pour N
    struct result m;
    ssize_t ret = read(master->pipeWM[0], &m, sizeof(struct result));
    myassert(ret == sizeof(struct result), "read result from worker");

    // Mise à jour des statistiques pour N
    if (m.isPrime) {
        master->nbPrimes++;
        if (m.n > master->highestPrime)
            master->highestPrime = m.n;
    }

    // Mise à jour de M : tous les nombres jusqu'à N ont été testés
    master->M = N;

    // Envoyer le résultat final du nombre N au client via la FIFO
    myassert(write(fdWrite, &m.isPrime, sizeof(int)) == sizeof(int),
             "write result to client");
}

/************************************************************************
 * Usage et analyse des arguments passés en ligne de commande
 ************************************************************************/

static void usage(const char *exeName, const char *message)
{
    fprintf(stderr, "usage : %s\n", exeName);
    if (message != NULL)
        fprintf(stderr, "message : %s\n", message);
    exit(EXIT_FAILURE);
}


/************************************************************************
 * boucle principale de communication avec le client
 ************************************************************************/
void loop() {
    master_t master = {0,1,0,0}; // initialisation des données du master

    // récupérer le sémaphore
    master.semId = semget(SEM_KEY, 1, 0);
    myassert(master.semId != -1, "semget master failed ");

    while (1) {
        // ouvrir les tubes nommés
        int fdFromClient  = open(CLIENT_TO_MASTER_PIPE, O_RDONLY);
        myassert(fdFromClient != -1, "open CLIENT_TO_MASTER_PIPE");

        int fdToClient = open(MASTER_TO_CLIENT_PIPE, O_WRONLY);
        myassert(fdToClient != -1, "open MASTER_TO_CLIENT_PIPE");

        // lire l'ordre du client
        int order;
        read(fdFromClient, &order, sizeof(order));

        switch(order) {
            case ORDER_STOP: {
                // envoyer stop au worker
                int fdToWorker = open("worker_pipe_in", O_WRONLY); // pipe vers worker
                myassert(fdToWorker != -1, "open worker_pipe_in");
                int stopSignal = ORDER_STOP;
                write(fdToWorker, &stopSignal, sizeof(stopSignal));
                close(fdToWorker);

                // envoyer accusé de réception au client
                int ack = 1;
                write(fdToClient, &ack, sizeof(ack));
                break;
            }

            case ORDER_COMPUTE_PRIME: {
                // récupérer le nombre N
                int N;
                read(fdFromClient, &N, sizeof(N));

                // envoyer tous les nombres manquants aux workers
                for (int i = master.highestSent + 1; i <= N; ++i) {
                    int fdToWorker = open("worker_pipe_in", O_WRONLY);
                    myassert(fdToWorker != -1, "open worker_pipe_in");
                    write(fdToWorker, &i, sizeof(i));

                    // récupérer réponse du worker
                    int isPrime = 0;
                    int fdFromWorker = open("worker_pipe_out", O_RDONLY);
                    myassert(fdFromWorker != -1, "open worker_pipe_out");
                    read(fdFromWorker, &isPrime, sizeof(isPrime));
                    close(fdFromWorker);

                    if (isPrime) {
                        master.primeCount++;
                        master.highestPrime = i;
                    }
                    close(fdToWorker);
                }
                master.highestSent = N;

                // envoyer la réponse finale au client
                int result = master.highestPrime;
                write(fdToClient, &result, sizeof(result));
                break;
            }

            case ORDER_HOW_MANY_PRIME: {
                int result = master.primeCount;
                write(fdToClient, &result, sizeof(result));
                break;
            }

            case ORDER_HIGHEST_PRIME: {
                int result = master.highestPrime;
                write(fdToClient, &result, sizeof(result));
                break;
            }

            default:
                fprintf(stderr, "ordre inconnu reçu: %d\n", order);
                break;
        }

        // 3. fermer les tubes
        close(fdFromClient);
        close(fdToClient);

        // 4. attendre que le client autorise la prochaine itération (sémaphore)
        struct sembuf P = {0, -1, 0};
        semop(master.semId, &P, 1);
    }
}


/************************************************************************
 * Fonction principale
 ************************************************************************/

int main(int argc, char * argv[])
{
    // Vérification des arguments
    if (argc != 1)
    {
        fprintf(stderr, "usage : %s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Déclaration et initialisation du contexte du master
    master_t master = {
        .M = 0,
        .nbPrimes = 0,
        .highestPrime = 0,
        .pipeMW = {0, 0},
        .pipeWM = {0, 0},
        .fifo1_name = NULL,
        .fifo2_name = NULL,
        .sem_mutex = -1,
        .sem_sync = -1
    };

    // Initialisation des sémaphores et des FIFO
    init_ipc(&master);

    // Création des pipes pour communiquer avec le worker
    myassert(pipe(master.pipeMW) != -1, "pipe master->worker");
    myassert(pipe(master.pipeWM) != -1, "pipe worker->master");

    // Création du worker (premier worker pour le pivot 2)
    pid_t child = fork();
    myassert(child != -1, "fork worker");

    if (child == 0) {
        // Code exécuté par le worker fils
        char p[32], fdi[32], fdm[32];

        // Le premier pivot est 2
        snprintf(p, sizeof p, "%d", 2);

        // Passage des FDs au worker
        snprintf(fdi, sizeof fdi, "%d", master.pipeMW[0]); // Lecture du Master
        snprintf(fdm, sizeof fdm, "%d", master.pipeWM[1]); // Écriture vers le Master

        // Fermeture des extrémités inutilisées dans le worker
        close(master.pipeMW[1]);
        close(master.pipeWM[0]);

        // Lancement du programme worker
        execl("./worker", "worker", p, fdi, fdm, (char *)NULL);

        perror("execl worker");
        exit(EXIT_FAILURE);
    }

    // Code exécuté par le master (parent)
    close(master.pipeMW[0]); // fermer la lecture du pipe master->worker
    close(master.pipeWM[1]); // fermer l'écriture du pipe worker->master

    // Boucle principale : traitement des commandes du client
    while (true) {
        // Ouverture des FIFOs pour la communication
        int fdRead = open(master.fifo1_name, O_RDONLY);
        myassert(fdRead != -1, "open fifo client->master for read");

        int fdWrite = open(master.fifo2_name, O_WRONLY);
        myassert(fdWrite != -1, "open fifo master->client for write");

        int order;
        // Lecture de l'ordre
        ssize_t r = read(fdRead, &order, sizeof(int));
        myassert(r == sizeof(int), "read order from client");

        // Traitement de l'ordre
        if (order == ORDER_STOP) {
            client_stop(&master, fdWrite, fdRead);
            break; // sortie de la boucle principale

        } else if (order == ORDER_COMPUTE_PRIME) {
            int N;
            r = read(fdRead, &N, sizeof(int));
            myassert(r == sizeof(int), "read N from client");

            client_compute_prime(&master, N, fdWrite);

        } else if (order == ORDER_HOW_MANY_PRIME) {
            if (write(fdWrite, &master.nbPrimes, sizeof(int)) != sizeof(int))
                perror("write nbPrimes to client");

        } else if (order == ORDER_HIGHEST_PRIME) {
            if (write(fdWrite, &master.highestPrime, sizeof(int)) != sizeof(int))
                perror("write highestPrime to client");
        }

        // Fermeture des FIFOs après traitement
        close(fdRead);
        close(fdWrite);

        // Attente de la synchronisation (client a lu la réponse)
        struct sembuf op = {0, -1, 0};
        myassert(semop(master.sem_sync, &op, 1) != -1, "semop sync wait");
    }

    // Nettoyage des IPC avant de quitter
    clean_ipc(&master);

    return EXIT_SUCCESS;
}

// N'hésitez pas à faire des fonctions annexes ; si les fonctions main
// et loop pouvaient être "courtes", ce serait bien
