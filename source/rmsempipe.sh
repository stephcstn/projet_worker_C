#!/bin/bash

DROITS=641
nb=`ipcs -s | grep $USER | grep $DROITS | wc -l`

if [ $nb -eq 0 ]
then
    echo "aucun sémaphore à détruire"
else
    pluriel=""
    if [ $nb -gt 1 ]; then pluriel="s"; fi
    echo "vous avez $nb sémaphore$pluriel non détruit$pluriel";

    for id in `ipcs -s | grep $USER | grep $DROITS | awk '{print $2;}'`
    do
        echo "  destruction sémaphore " $id
        ipcrm -s $id
    done

    nb=`ipcs -s | grep $USER | grep $DROITS | wc -l`
    pluriel=""
    if [ $nb -gt 1 ]; then pluriel="s"; fi
    echo "il reste $nb sémaphore$pluriel non détruit$pluriel";
fi


c2m="nom_du_tube_client_vers_master"
m2c="nom_du_tube_master_vers_client"

if [ ! -p $c2m ]
then
    echo "pas de tube client vers master"
else
    echo "tube client vers master détruit"
    /bin/rm $c2m
fi

if [ ! -p $m2c ]
then
    echo "pas de tube master vers client"
else
    echo "tube master vers client détruit"
    /bin/rm $m2c
fi

