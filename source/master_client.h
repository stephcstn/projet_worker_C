#ifndef CLIENT_CRIBLE
#define CLIENT_CRIBLE

// On peut mettre ici des éléments propres au couple master/client :
//    - des constantes pour rendre plus lisible les comunications
//    - des fonctions communes (création tubes, écriture dans un tube,
//      manipulation de sémaphores, ...)
#define CLIENT_TO_MASTER_PIPE "ctom.fifo"
#define MASTER_TO_CLIENT_PIPE "mtoc.fifo"

// Clés IPC pour les sémaphores
#define MUTEX_KEY 0x1234
#define SYNC_KEY 0x2345

// Noms des FIFOs
#define FIFO_CLIENT_MASTER "fifos/prime_client_master"
#define FIFO_MASTER_CLIENT "fifos/prime_master_client"

// ordres possibles pour le master
#define ORDER_NONE                0
#define ORDER_STOP               -1
#define ORDER_COMPUTE_PRIME       1
#define ORDER_HOW_MANY_PRIME      2
#define ORDER_HIGHEST_PRIME       3
#define ORDER_COMPUTE_PRIME_LOCAL 4   // ne concerne pas le master

// bref n'hésitez à mettre nombre de fonctions avec des noms explicites
// pour masquer l'implémentation


#endif
