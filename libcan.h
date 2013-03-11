/**
 * @file libcan.h
 *
 * @brief Encapsule tout le bas niveau CAN dans des fonctions plus
 * faciles à utiliser.
 *
 * @mainpage Librairie libCAN
 *
 * L'objectif est d'encapsuler la gestion du Bus CAN sous linux dans des
 * fonctions plus faciles d'utilisations.
 */

#ifndef __LIBCAN_H__
#define __LIBCAN_H__

#ifdef __cplusplus
extern "C"{
#endif

#include <net/if.h>
#include <linux/can.h>
#include "CServerTcpIP.h"


/**
* @brief Initialise la lib_can
*
* Ouvre un socket can en lecture écriture. Crée le thread principal de la
* lib_can. Crée une fifo pour la communication entre le programme appelant et la
* lib_can. Crée un timer pour l'appel périodique à la fonction d'envoi.
* Si la libcan est déjà active, alors il ne se passera rien.
*
* @param iface_can chaine pour l'interface CAN (ex : "/dev/can0")
*
* @returns 0 si OK, 1 si socket_can KO, 2 FIFO, 3 Thread can, 4 Timer, 5 libcan active
*/
int can_init(const char * iface_can);

/**
* @brief Termine la lib_can
*
* Attend la fin du thread. Ferme les fifos et le socket, supprime la fifo
* utilisée.
*
* @returns 0
*/
int can_close(void);


/**
* @brief Donne l'état de la lib_can.
*
* @returns 1 si la lib est lancée, 0 sinon.
*/
int can_isok(void);


/**
* @brief Envoi directement un message
*
* @param msg message CAN à envoyer
*
* @returns 0 si OK, 1 si erreur
*/
int can_send(struct can_frame msg);


/**
* @brief Initialise le lancement périodique d'un message CAN
*
* Associe un couple identifiant + peride à une zone mémoire.
* Après l'execution de cette fonction, toutes les période arrondie à 10 ms, la
* librairie envoi le message CAN ID avec les donneés de la zone mémoire.
*
* @param ID Identifiant CAN
* @param zone Zone mémoire à envoyer péridiquement
* @param zone_length Longueur de la zone
* @param period Période de l'envoi (granularité sur l'envoi : 10ms)
*
* @returns 1 si OK, !=0 si KO.
*/
int can_bind_send(unsigned short ID, void * zone, unsigned short zone_length,
                  unsigned long period);


/**
* @brief Bind un ID+masque à un espace mémoire et/ou un callback
*
* Associe un couple identifiant + masque à une zone mémoire et/ou à un callback.
* Après l'execution de cette fonction, si un message reçu respecte la condition
* "(ID_reçue && mask) == (ID_bind && mask)" alors :
* 	Si zone n'est pas NULL la mémoire est remplie
*	si callback n'est pas NULL, callback est appelé
*
* @param ID l'identifiant CAN
* @param mask le masque
* @param zone la zone mémoire à remplir
* @param zone_length la taille de la zone mémoire disponible
* @param callback fonction à appeler lors de la reception
*
* @returns 0 si OK, !=0 si KO
*
*/
int can_bind_receive(unsigned short ID, unsigned short mask, void * zone,
                     unsigned short zone_length,
		     void (*callback)(CServerTcpIP *this, struct can_frame cf));


#ifdef __cplusplus
}
#endif

#endif
