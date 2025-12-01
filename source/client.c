// ce fichier contient l'implémentation du client
// il gère les ordres envoyés au master et dispose d'un mode local multithread

#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>
#include <math.h>

#include "myassert.h"
#include "master_client.h"

// constantes pour les noms d'ordres
#define TK_STOP      "stop"
#define TK_COMPUTE   "compute"
#define TK_HOW_MANY  "howmany"
#define TK_HIGHEST   "highest"
#define TK_LOCAL     "local"

// analyse et vérification des arguments
static void usage(const char *exeName, const char *message)
{
    fprintf(stderr, "usage : %s <ordre> [<nombre>]\n", exeName);
    fprintf(stderr, "   ordre \"%s\" : arrêt master\n", TK_STOP);
    fprintf(stderr, "   ordre \"%s\" : calcul de nombre premier\n", TK_COMPUTE);
    fprintf(stderr, "                       <nombre> doit être fourni\n");
    fprintf(stderr, "   ordre \"%s\" : combien de nombres premiers calculés\n", TK_HOW_MANY);
    fprintf(stderr, "   ordre \"%s\" : quel est le plus grand nombre premier calculé\n", TK_HIGHEST);
    fprintf(stderr, "   ordre \"%s\" : calcul de nombres premiers en local\n", TK_LOCAL);
    if (message != NULL)
        fprintf(stderr, "message : %s\n", message);
    exit(EXIT_FAILURE);
}

static int parseArgs(int argc, char * argv[], int *number)
{
    int order = ORDER_NONE;
    if ((argc != 2) && (argc != 3))
        usage(argv[0], "Nombre d'arguments incorrect");
    if (strcmp(argv[1], TK_STOP) == 0)
        order = ORDER_STOP;
    else if (strcmp(argv[1], TK_COMPUTE) == 0)
        order = ORDER_COMPUTE_PRIME;
    else if (strcmp(argv[1], TK_HOW_MANY) == 0)
        order = ORDER_HOW_MANY_PRIME;
    else if (strcmp(argv[1], TK_HIGHEST) == 0)
        order = ORDER_HIGHEST_PRIME;
    else if (strcmp(argv[1], TK_LOCAL) == 0)
        order = ORDER_COMPUTE_PRIME_LOCAL;
    if (order == ORDER_NONE)
        usage(argv[0], "ordre incorrect");
    if ((order == ORDER_STOP) && (argc != 2))
        usage(argv[0], "il ne faut pas de second argument");
    if ((order == ORDER_COMPUTE_PRIME) && (argc != 3))
        usage(argv[0], "il faut le second argument");
    if ((order == ORDER_HOW_MANY_PRIME) && (argc != 2))
        usage(argv[0], "il ne faut pas de second argument");
    if ((order == ORDER_HIGHEST_PRIME) && (argc != 2))
        usage(argv[0], "il ne faut pas de second argument");
    if ((order == ORDER_COMPUTE_PRIME_LOCAL) && (argc != 3))
        usage(argv[0], "il faut le second argument");
    if ((order == ORDER_COMPUTE_PRIME) || (order == ORDER_COMPUTE_PRIME_LOCAL))
    {
        *number = (int)strtol(argv[2], NULL, 10);
        if (*number < 2)
            usage(argv[0], "le nombre doit être >= 2");
    }
    return order;
}
/* structure pour passer les arguments aux threads */
typedef struct {
    bool *prime_table;      // tableau partagé des booléens (vrai = premier)
    int N;                  // borne supérieure pour le crible
    int divisor;            // diviseur traité par ce thread
    pthread_mutex_t *mutex; // mutex pour protéger le tableau partagé
} ThreadData;

/* fonction exécutée par chaque thread */
void *sieve_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    // on commence à marquer les multiples du diviseur à partir de 2*divisor
    int start = 2 * data->divisor;

    while (start <= data->N) {
        // section critique : modification du tableau partagé
        int ret = pthread_mutex_lock(data->mutex);
        myassert(ret == 0, "pthread_mutex_lock failed");

        // marquer le multiple comme non premier
        data->prime_table[start] = false;

        ret = pthread_mutex_unlock(data->mutex);
        myassert(ret == 0, "pthread_mutex_unlock failed");

        start += data->divisor; // passer au multiple suivant
    }

    return NULL;
}

/* fonction principale pour calculer localement les nombres premiers jusqu'à N */
void compute_local_primes(int N) {
    if (N < 2) {
        printf("Pas de nombres premiers jusqu'à %d\n", N);
        return;
    }

    // création du tableau des booléens, toutes les cases initialisées à true
    bool *prime_table = malloc((N + 1) * sizeof(bool));
    if (!prime_table) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    for (int i = 2; i <= N; i++)
        prime_table[i] = true;

    // nombre de threads : un pour chaque diviseur de 2 à sqrt(N)
    int sqrtN = (int)sqrt(N);
    int nb_threads = sqrtN - 1;

    // allocation dynamique des threads et de leurs arguments
    pthread_t *threads = malloc(nb_threads * sizeof(pthread_t));
    ThreadData *thread_args = malloc(nb_threads * sizeof(ThreadData));
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // mutex local pour protéger le tableau

    // création des threads
    for (int i = 2; i <= sqrtN; i++) {
        int idx = i - 2; // index dans le tableau des threads
        thread_args[idx].prime_table = prime_table;
        thread_args[idx].N = N;
        thread_args[idx].divisor = i;
        thread_args[idx].mutex = &mutex;

        int ret = pthread_create(&threads[idx], NULL, sieve_thread, &thread_args[idx]);
        myassert(ret == 0, "pthread_create failed");
    }

    // attendre que tous les threads aient fini
    for (int i = 0; i < nb_threads; i++) {
        int ret = pthread_join(threads[i], NULL);
        myassert(ret == 0, "pthread_join failed");
    }

    // affichage des nombres premiers
    printf("Nombres premiers jusqu'à %d :\n", N);
    for (int i = 2; i <= N; i++) {
        if (prime_table[i])
            printf("%d ", i);
    }
    printf("\n");

    // nettoyage
    free(prime_table);
    free(threads);
    free(thread_args);
    pthread_mutex_destroy(&mutex);
}

int main(int argc, char * argv[])
{
    // lecture du nombre ou de l'ordre depuis la ligne de commande
    int number = 0;
    int order = parseArgs(argc, argv, &number);

    // si on veut calculer les nombres premiers localement
    if (order == ORDER_COMPUTE_PRIME_LOCAL) {
        compute_local_primes(number);
        return EXIT_SUCCESS;
    }

    // sinon, on utilise le master
    int sem_mutex = semget(MUTEX_KEY, 1, 0); // récupérer le sémaphore mutex
    myassert(sem_mutex != -1, "semget mutex");
    
    int sem_sync = semget(SYNC_KEY, 1, 0); // récupérer le sémaphore sync
    myassert(sem_sync != -1, "semget sync");

    // prendre le mutex avant d'accéder aux FIFOs
    struct sembuf operationPrendre = {0, -1, 0};
    semop(sem_mutex, &operationPrendre, 1);

    const char *fifo1 = FIFO_CLIENT_MASTER; // FIFO pour écrire vers le master
    const char *fifo2 = FIFO_MASTER_CLIENT; // FIFO pour lire depuis le master

    int fdWrite = open(fifo1, O_WRONLY); // ouverture FIFO écriture
    myassert(fdWrite != -1, "open fifo write");

    int fdRead = open(fifo2, O_RDONLY); // ouverture FIFO lecture
    myassert(fdRead != -1, "open fifo read");

    write(fdWrite, &order, sizeof(int)); // envoyer l'ordre au master

    if (order == ORDER_COMPUTE_PRIME)
        write(fdWrite, &number, sizeof(int)); // envoyer le nombre à tester

    int response;
    read(fdRead, &response, sizeof(int)); // lire la réponse du master

    // affichage du résultat selon l'ordre
    if (order == ORDER_COMPUTE_PRIME) {
        if (response)
            printf("%d est premier\n", number);
        else
            printf("%d n'est pas premier\n", number);
    } else if (order == ORDER_HOW_MANY_PRIME) {
        printf("%d\n", response);
    } else if (order == ORDER_HIGHEST_PRIME) {
        printf("%d\n", response);
    }

    // fermeture des FIFOs
    int ret = close(fdWrite);
    myassert(ret != -1, "close fifo write");

    ret = close(fdRead);
    myassert(ret != -1, "close fifo read");

    // libération des sémaphores
    struct sembuf operationVendre = {0, 1, 0};
    semop(sem_mutex, &operationVendre, 1);
    semop(sem_sync, &operationVendre, 1);

    return EXIT_SUCCESS;
}