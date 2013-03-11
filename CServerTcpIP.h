/*
 *	This file is part of libpegase.
 *
 *	Libpegase is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	Libpegase is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	Copyright (C) 2009	William MARTIN <william.martin@lcpc.fr>
 *						Vincent LE CAM <vincent.le-cam@lcpc.fr>
 *						Laurent LE MARCHAND <laurent.lemarchand@lcpc.fr>
 *						Mathieu LE PEN <mathieu.lepen@lcpc.fr>
 */

/*!
 *	\file CServerTcpIP.c
 *	\brief Serveur TCP/IP multiclient
 *	\version 1.0
 *	\date 07 september 2009
 * 
 *	08 september 2009	- WM-	Initial commit from biblos (v3)
 *  27 octobre 2011		- VLC - Mise à jour et explicitation de la libraire PEGASE
 */

#ifndef _SERVEURTCPIP_H_
#define _SERVEURTCPIP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

/* 
 *	Structure Client
 *	Represente les informations d'un client TCP/IP en particulier: IP / port
 */
typedef struct _Client Client;
struct _Client {
	int fd;				/* file descripteur du client */
	char *adresseIP;	/* Adresse IP du client connecté */
	unsigned int port;	/* Port distant du client (different du port local du serveur) */
	Client *next;		/* pointeur sur le client suivant: liste chaînée */
}; 

typedef struct _CServerTcpIP CServerTcpIP;

/* 
 *	Prototype de fonction de Callback lors de la connexion d'un client IP sur l'objet CServerTcpIP
 *
 *  this:			Pointeur sur l'objet de type CServerTcpIP	
 *	from:			Pointeur sur objet de type Client identifiant le client IP qui s'est connecté
 *	pdata:			Optionnal Private pointer
 */
typedef void (*CServerTcpIP_connect_t) (CServerTcpIP *this, Client *from, void *pdata);


/* 
 *	Prototype de fonction de Callback pour la réception de data d'un client IP sur l'objet CServerTcpIP
 *
 *	Buffer:			Pointer on received data
 *	Buffer_size:	Size of received data
 *  this:			Pointeur sur l'objet de type CServerTcpIP	
 *	from:			Pointeur sur objet de type Client identifiant le client IP qui a envoyé la data
 *	pdata:			Optionnal Private pointer
 */
typedef void (*CServerTcpIP_rx_t) (char *buffer, unsigned int buffer_size, CServerTcpIP *this, Client *from, void *pdata);


/******************************************************************************
 *
 *	Structure de donnée de la classe CServerTcpIP 
 *
 *
 ******************************************************************************/

struct _CServerTcpIP {

	 /* Méthodes */
	 /************/

	// Destructeur de la classe CServerTcpIP
	void (*Free) (CServerTcpIP *this);

	// Fonction d'envoi TCP/IP de données de l'objet CServerTcpIP au client 'destinataire'
	//	-destinataire:	pointeur sur un objet Client cible ou tous les clients connectés si NULL (BROADCAST)
	//	-buffer:		adresse de la data à envoyer
	//	-buffer_size:	nb d'octets de 'buffer' à envoyer
	//	-retour:		-1 si erreur, buffer_size si ok
	int (*Send) (CServerTcpIP *this, Client *destinataire, char *buffer, unsigned int buffer_size);

	// Renvoie à tout moment le nb de clients connectés au serveur
	int (*GetNbClientsConnected) (CServerTcpIP *this);
	
	// Démarre l'écoute de l'objet serveur CServerTcpIP sur le port: 'port'
	//	-retour:		-1 si erreur, 0 si ok
	int (*Start) (CServerTcpIP *this, unsigned short port);

	// Arrête l'écoute de l'objet serveur CServerTcpIP
	//	-retour:		-1 si erreur, 0 si ok
	int (*Stop) (CServerTcpIP *this);

	 /* Attributs */
	 /*************/
	int m_fdListen;			/* Descripteur de la socket d'écoute */
	pthread_t m_threadListen;	/* Le thread qui écoute et répond aux demandes de connexions des clients */
	Client *m_clistClients;		/* Liste chainée des clients connectés */
	int m_iClientNumber;		/* Taille de la liste chainée */
	CServerTcpIP_rx_t m_callback;	/* Fonction de callback pour le traitement des données reçues */
	CServerTcpIP_connect_t m_connect_callback;	/* Fonction de callback pour les demandes de connexions clients */
	void *m_pvPrivateData;		/* Pointeur optionnel donné au constructeur et repassé aux callbacks */
};

/*
 *	Constructeur de la classe CServerTcpIP
 *
 *	-rx_callback:		callback selon prototype ci-dessus appelée lorsque de la data est reçue par le Serveur
 *	-connect_callback:	callback selon prototype ci-dessus appelée lors d'une connexion de client au Serveur
 *	-pdata:				pointeur optionnel repassé aux callbaks lors de l'appel
 */
extern CServerTcpIP *CServerTcpIP_New (CServerTcpIP_rx_t rx_callback, CServerTcpIP_connect_t connect_callback, void *pdata);
extern CServerTcpIP *this;

#ifdef __cplusplus
}
#endif
#endif /* _SERVEURTCPIP_H_ */

