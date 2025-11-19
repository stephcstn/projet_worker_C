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

// structure pour le master
typedef struct {
    int semId;
    int highestSent;     // plus grand nombre envoyé aux workers
    int highestPrime;    // plus grand nombre premier trouvé
    int primeCount;      // nombre total de nombres premiers
} master_t;

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
    if (argc != 1)
        usage(argv[0], NULL);

    // - création des sémaphores
    int semId = semget(SEM_KEY, 1, IPC_CREAT | 0641);
    myassert(semId != -1, "échec de semget dans master");

    int ret = semctl(semId, 0, SETVAL, 1);
    myassert(ret != -1, "échec de semctl dans master");

    // - création des tubes nommés
    ret = mkfifo(MASTER_TO_CLIENT_PIPE, 0641);
    myassert(ret == 0, "échec de la création du tube masterToClient dans master");

    ret = mkfifo(CLIENT_TO_MASTER_PIPE, 0641);
    myassert(ret == 0, "échec de la création du tube clientToMaster dans master");
    
/*   //  - création du premier worker
    int firstWorkerPid = fork();
    myassert(firstWorkerPid != -1, "échec du fork du premier worker dans master");

    if (firstWorkerPid == 0)
    {
        // processus fils : exécution du worker
        execlp("./worker", "worker", "2",
               /* fdIn */ 
               /* fdToMaster */ 
              // (char *) NULL);
        // si on arrive ici c'est qu'il y a eu une erreur
        //perror("échec de execlp dans master pour le premier worker");
      //  exit(EXIT_FAILURE);
    //}
*/
    // boucle infinie
    loop(/* paramètres */);

    // destruction des tubes nommés, des sémaphores, ...
    ret = unlink(MASTER_TO_CLIENT_PIPE);
    myassert(ret == 0, "échec de la suppression du tube masterToClient dans master");

    ret = unlink(CLIENT_TO_MASTER_PIPE);
    myassert(ret == 0, "échec de la suppression du tube clientToMaster dans master");

    ret = semctl(semId, 0, IPC_RMID);
    myassert(ret != -1, "échec de la destruction des sémaphores dans master");

    return EXIT_SUCCESS;
}

// N'hésitez pas à faire des fonctions annexes ; si les fonctions main
// et loop pouvaient être "courtes", ce serait bien
