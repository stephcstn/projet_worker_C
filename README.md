# Worker

[![Langage](https://img.shields.io/badge/langage-C99-blue)](https://en.cppreference.com/w/c/99)
[![Build](https://img.shields.io/badge/build-Makefile-darkgreen)](./source/Makefile)
[![Plateforme](https://img.shields.io/badge/plateforme-Linux%20%2F%20POSIX-lightgrey)](#compilation)

**Worker** est un projet pédagogique en C qui explore deux approches du calcul de nombres premiers : un service client/master s’appuyant sur une chaîne de processus workers et un mode local parallèle avec des threads POSIX.

## Contexte et objectifs techniques

Le calcul des nombres premiers sert ici de support à l’étude de la programmation système. Le projet sépare l’interface de commande, l’orchestration du calcul et les unités de traitement, afin d’expérimenter les échanges entre processus et la synchronisation entre clients.

Les principaux concepts mis en œuvre sont :

- **Processus et workers** : le master crée un premier processus avec `fork` et le lance avec `exec`. Lorsqu’un nouveau nombre premier est identifié, un worker supplémentaire est créé pour le traiter comme pivot dans la chaîne.
- **Communication inter-processus** : des pipes anonymes relient le master et les workers ; des FIFOs nommées transportent les commandes et les réponses entre le client et le master.
- **Synchronisation IPC** : des sémaphores System V protègent l’accès aux FIFOs et coordonnent les échanges client/master.
- **Threads et mémoire partagée** : le mode `local` applique un crible parallèle avec des threads `pthread`, un tableau dynamique partagé et un mutex.
- **Cycle de vie des ressources** : le master arrête les workers et nettoie les FIFOs et sémaphores à la commande `stop` ; le mode local libère ses allocations et attend la fin de ses threads.

Le mode en chaîne illustre la distribution du filtrage entre processus ; il ne s’agit pas d’un ordonnanceur généraliste ni d’un système de calcul distribué sur plusieurs machines.

## Architecture et organisation

```text
.
├── .gitignore
├── README.md
└── source/
    ├── Makefile
    ├── client.c
    ├── master.c
    ├── worker.c
    ├── master_client.c
    ├── master_client.h
    ├── master_worker.c
    ├── master_worker.h
    ├── myassert.c
    ├── myassert.h
    ├── config.h
    ├── rmsempipe.sh
    └── 0README
```

Le répertoire `source/fifos/` est nécessaire à l’exécution du master : les FIFOs y sont créées au démarrage. Il est créé manuellement dans les commandes ci-dessous et n’est pas requis pour compiler.

| Fichier | Responsabilité |
|---|---|
| `source/client.c` | Analyse les commandes. Le mode service communique avec le master via les FIFOs et sémaphores ; le mode `local` calcule les nombres premiers avec `pthread`. |
| `source/master.c` | Maintient les statistiques du calcul, reçoit les commandes des clients, gère les IPC et pilote la chaîne de workers. |
| `source/worker.c` | Teste les nombres reçus par rapport à son pivot premier, transmet les candidats au worker suivant et crée un nouveau worker lorsqu’un nouveau premier est découvert. |
| `source/master_client.h` | Définit les noms de FIFOs, clés de sémaphores et codes de commande utilisés par le client et le master. |
| `source/master_worker.h` | En-tête partagé prévu pour les éléments communs au master et aux workers. |
| `source/master_client.c`, `source/master_worker.c` | Modules d’implémentation communs, actuellement réduits à des squelettes. |
| `source/myassert.c`, `source/myassert.h` | Fournissent `myassert`, une assertion qui affiche le fichier, la fonction, la ligne et le contexte système en cas d’échec. |
| `source/config.h` | Contient la configuration optionnelle de traces (`VERBOSE`). |
| `source/Makefile` | Compile les trois exécutables et définit les règles `clean` et `distclean`. |

### Flux de communication

```text
Client ── FIFO client → master ──┐
        ← FIFO master → client ──┤
        ── sémaphores System V ──┘
                                  Master
                                    │ pipes anonymes
                                    ▼
                              Worker (pivot 2)
                                    │ pipe vers le suivant
                                    ▼
                              Worker (pivot suivant) …
```

Le mode `local` est indépendant du service master : le client crée les threads de criblage et partage entre eux le tableau des candidats.

## Collaboration en binôme

Le projet a été réalisé par **Stéphane** et **Basile**, avec une répartition du travail par modules et un suivi des changements dans Git :

- **Stéphane** : avancement du client, du calcul local et de la configuration de l’IPC client/master ;
- **Basile** : finalisation des modules master et worker.

Cette répartition a permis de travailler en parallèle sur l’interface et le calcul, puis de réunir ces parties autour des contrats définis dans les en-têtes communs.

## Fiabilité et gestion de la mémoire

Le code utilise `myassert` pour signaler certains échecs d’appels système et d’opérations `pthread`. Dans le mode local, le tableau des candidats, les structures d’arguments et le tableau des identifiants de threads sont alloués dynamiquement ; les threads sont joints et ces allocations sont libérées à la fin du calcul.

Le Makefile active `-Wall -Wextra -pedantic` et la norme `-std=c99`. Il **n’active pas `-Werror`**. Aucun rapport d’exécution Valgrind n’étant fourni avec le dépôt, l’absence de fuite mémoire n’est pas présentée comme vérifiée. Le contrôle des retours d’appels système n’est pas uniforme dans tous les chemins du code ; une revue et des tests complémentaires restent nécessaires avant de considérer le projet comme validé pour un usage de production.

Pour effectuer une vérification mémoire du mode local sous Linux, après compilation :

```bash
cd source
valgrind --leak-check=full --show-leak-kinds=all ./client local 1000
```

## Compilation et utilisation

### Prérequis

- Linux avec les interfaces POSIX et System V IPC utilisées par le projet (`fork`, `exec`, pipes, FIFOs, sémaphores et threads POSIX) ;
- GCC et GNU Make ;
- les bibliothèques système `pthread` et `libm` (`-lpthread` et `-lm` dans le Makefile).

Le Makefile compile en C99 avec les options `-g -Wall -Wextra -pedantic -std=c99 -fPIC`. Les binaires `client`, `master` et `worker` sont produits dans `source/`.

### Compiler depuis la racine

```bash
make -C source
```

### Nettoyer les fichiers intermédiaires

```bash
make -C source clean
```

`clean` supprime les fichiers objets (`.o`) et de dépendances (`.d`). La cible `distclean` supprime également les exécutables ; elle n’est donc pas nécessaire pour un nettoyage courant.

### Mode local multithread

Ce mode ne nécessite pas de lancer le master :

```bash
cd source
./client local 30
```

Sortie attendue :

```text
Nombres premiers jusqu'à 30 :
2 3 5 7 11 13 17 19 23 29
```

### Mode client/master avec workers

Dans un premier terminal, depuis la racine du dépôt :

```bash
mkdir -p source/fifos
cd source
./master
```

Le master reste actif et attend les commandes. Dans un second terminal :

```bash
cd source
./client compute 30
./client howmany
./client highest
./client stop
```

Après le calcul jusqu’à 30 inclus, les réponses attendues sont :

```text
30 n'est pas premier
10
29
```

La commande `stop` demande l’arrêt du service ; le master transmet le signal d’arrêt aux workers, attend leur terminaison et supprime les ressources IPC qu’il a créées. Lancez le client depuis `source/` : les chemins des FIFOs et l’exécutable `./worker` sont relatifs au répertoire courant.

Commandes disponibles :

| Commande | Effet |
|---|---|
| `./client compute <N>` | Vérifie si `N` est premier et complète le calcul jusqu’à `N`. |
| `./client howmany` | Affiche le nombre de nombres premiers comptabilisés par le master. |
| `./client highest` | Affiche le plus grand nombre premier trouvé par le master. |
| `./client local <N>` | Calcule et affiche les nombres premiers jusqu’à `N` avec le crible local multithread. |
| `./client stop` | Arrête le master et ses workers. |

## Auteurs

- **Stéphane Castan**
- **Basile Dufrene**
