#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "myassert.h"

#include "master_worker.h"

/************************************************************************
 * Données persistantes d'un worker
 ************************************************************************/

// on peut ici définir une structure stockant tout ce dont le worker
// a besoin : le nombre premier dont il a la charge, ...

struct result {
    int n;
    int isPrime;
};

typedef struct {
    int n_prime;
    int MasterToWorker;
    int WorkerToMaster;
    int WorkerToNext;
    pid_t nextPid;
} worker_t;


/************************************************************************
 * Usage et analyse des arguments passés en ligne de commande
 ************************************************************************/

static void usage(const char *exeName, const char *message)
{
    fprintf(stderr, "usage : %s <n> <fdIn> <fdToMaster>\n", exeName);
    fprintf(stderr, "   <n> : nombre premier géré par le worker\n");
    fprintf(stderr, "   <fdIn> : canal d'entrée pour tester un nombre\n");
    fprintf(stderr, "   <fdToMaster> : canal de sortie pour indiquer si un nombre est premier ou non\n");
    if (message != NULL)
        fprintf(stderr, "message : %s\n", message);
    exit(EXIT_FAILURE);
}

static void parseArgs(int argc, char * argv[] /*, structure à remplir*/)
{
    if (argc != 4)
        usage(argv[0], "Nombre d'arguments incorrect");

    // initialisation du nombre premier de ce worker
    worker->n_prime = (int)strtol(argv[1], NULL, 10);

    // initialisation des pipes : master -> worker
    worker->MasterToWorker = (int)strtol(argv[2], NULL, 10);

    // initialisation des pipes : worker -> master
    worker->WorkerToMaster = (int)strtol(argv[3], NULL, 10);

    // aucun worker suivant pour le moment
    worker->WorkerToNext = -1;

    // PID du worker suivant initialisé à 0
    worker->nextPid = 0;
}

/* Envoie le résultat d'un nombre testé au master */
static void send_result(struct worker_t *worker, int n, int isPrime)
{
    struct result m;
    m.n = n;          // nombre testé
    m.isPrime = isPrime; // résultat (1 = premier, 0 = non premier)
    write(worker->WorkerToMaster, &m, sizeof(struct result)); // envoyer au master
}

/* Crée un nouveau worker pour le prochain nombre premier trouvé */
static void create_next_worker(struct worker_t *worker, int newPrime)
{
    int pipeToNext[2]; 
    pipe(pipeToNext); // créer un pipe entre ce worker et le prochain

    pid_t pid = fork(); // créer un nouveau processus worker
    if (pid == 0) { // code du worker enfant
        char pbuf[32], fdi[32], fdm[32];
        snprintf(pbuf, sizeof pbuf, "%d", newPrime);           // nombre premier pour le nouveau worker
        snprintf(fdi, sizeof fdi, "%d", pipeToNext[0]);        // lecture du pipe
        snprintf(fdm, sizeof fdm, "%d", worker->WorkerToMaster); // pipe vers master

        close(pipeToNext[1]); // fermer l'extrémité d'écriture dans l'enfant

        // remplacer le processus par un nouveau worker
        execl("./worker", "worker", pbuf, fdi, fdm, (char *)NULL);
        perror("execl worker"); // si échec
        exit(EXIT_FAILURE);
    }

    // code du worker parent
    close(pipeToNext[0]);         // fermer l'extrémité lecture du pipe
    worker->WorkerToNext = pipeToNext[1]; // enregistrer l'écriture vers le prochain worker
    worker->nextPid = pid;        // stocker le PID du prochain worker
}

/************************************************************************
 * Boucle principale de traitement
 ************************************************************************/

void loop(struct worker_t *worker)
{
    // boucle infinie :
    //    attendre l'arrivée d'un nombre à tester
    //    si ordre d'arrêt
    //       si il y a un worker suivant, transmettre l'ordre et attendre sa fin
    //       sortir de la boucle
    //    sinon c'est un nombre à tester, 4 possibilités :
    //           - le nombre est premier
    //           - le nombre n'est pas premier
    //           - s'il y a un worker suivant lui transmettre le nombre
    //           - s'il n'y a pas de worker suivant, le créer
}

/************************************************************************
 * Programme principal
 ************************************************************************/

int main(int argc, char * argv[])
{
    parseArgs(argc, argv /*, structure à remplir*/);
    
    // Si on est créé c'est qu'on est un nombre premier
    // Envoyer au master un message positif pour dire
    // que le nombre testé est bien premier

    loop(/* paramètres */);

    // libérer les ressources : fermeture des files descriptors par exemple

    return EXIT_SUCCESS;
}
