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
 *	08 september 2009 - Initial commit from biblos (v3)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <errno.h>

#include "debug.h"
#include "CServerTcpIP.h"

#ifndef CSERVERTCPIP_RX_BUFFER_SIZE
	#define CSERVERTCPIP_RX_BUFFER_SIZE	(10*1024)
#endif

#ifndef CSERVERTCPIP_LISTEN_QUEUE_SIZE
	#define CSERVERTCPIP_LISTEN_QUEUE_SIZE	(512)
#endif


static int
CServerTcpIP_GetNbClientsConnected (CServerTcpIP *this)
{
	return this->m_iClientNumber;
}

/*
 *	Linked list methods
 *	Add / Delete / Free / Find
 *	
 */
Client *CServerTcpIP_FindClient (CServerTcpIP* this, int fd)
{
	if (fd < 0)
		return (Client *) NULL;

	if (this->m_clistClients == (Client *) NULL) {
		return (Client *) NULL;
	} else {
		Client *curseur = this->m_clistClients;

		do {
			if (curseur->fd == fd) {
				return curseur;
			}

			curseur = curseur->next;
		} while (curseur != (Client *) NULL);
	}

	return (Client *) NULL;
}

void CServerTcpIP_FreeClient (Client *client)
{
	shutdown (client->fd, SHUT_RDWR);
	DEBUG ("Close fd=%d\n", client->fd);
	close (client->fd);
	free (client->adresseIP);
	free (client);
}


void CServerTcpIP_AddClient (CServerTcpIP *this, Client *client)
{
	/*
	 *	Vérification des paramètres
	 */
	if (this == (CServerTcpIP *) NULL)
		return;

	if (client == (Client *) NULL)
		return;

	/*
	 *	Recherche du dernier maillon
	 */
	if (this->m_clistClients == (Client *) NULL) {
		this->m_clistClients = client;
	} else {
		Client *curseur = this->m_clistClients;

		while (curseur->next != (Client *) NULL) {
			curseur = curseur->next;
		}

		curseur->next = client;
	}

	/*
	 *	Incrémente le nombre de client connecté
	 */
	this->m_iClientNumber = this->m_iClientNumber + 1;
}


void CServerTcpIP_DelClient (CServerTcpIP *this, Client *client)
{
	Client *curseur;
	Client *curseur_prec = NULL;

	/*
	 *	Vérification des paramètres
	 */
	if (this == (CServerTcpIP *) NULL)
		return;

	if (client == (Client *) NULL)
		return;

	/*
	 *	Recherche du client dans la liste
	 */
	/* Cas de la liste vide */
	if (this->m_clistClients == (Client *) NULL) {
		DEBUG ("Client fantome !\n");
		return;
	}

	/* Cas particulier pour le 1er de la liste */
	if (this->m_clistClients == client) {
		CServerTcpIP_FreeClient (client);
		this->m_clistClients = client->next;
		this->m_iClientNumber = this->m_iClientNumber - 1;
		return;
	}

	curseur = this->m_clistClients->next;
	while (curseur != client) {
		curseur_prec = curseur;
		curseur = curseur->next;
		if (curseur == (Client *) NULL){
			DEBUG ("Client fantome !\n");
			return;
		}
	}
	curseur_prec->next = curseur->next;
	this->m_iClientNumber = this->m_iClientNumber - 1;
	CServerTcpIP_FreeClient (curseur);
}



static int
CServerTcpIP_Stop (CServerTcpIP *this)
{
	int ret;
	
	/* Check for unstarted listen socket */
	if (this->m_fdListen < 0) {
		DEBUG ("Warning listen socket may be not started !\n");
		return -1;
	}

	/* Stop Runtime */
	ret = pthread_cancel (this->m_threadListen);
	if (ret < 0) {
		DEBUG ("No thread to stop !\n");
	} else {
		ret = pthread_join (this->m_threadListen, NULL);
		if (ret == 0 || ret == -ESRCH) {
			DEBUG ("Reception thread stopped\n");
		} else {
			DEBUG ("Can't join the Reception thread !\n");
		}
	}

	/* Stop the listen socket */
	shutdown (this->m_fdListen, SHUT_RDWR);
	close (this->m_fdListen);
	this->m_fdListen = -1;
	
	/* Stop all active connection */
	while (this->m_clistClients != (Client *)NULL) {
		DEBUG_FLOOD ("this->m_clistClients = %d\n", this->m_clistClients);
		CServerTcpIP_DelClient (this, this->m_clistClients);
	}

	return 0;
}


/*
 *	Fonction	: runtime
 *	Description	: Thread d'écoute des descripteurs de fichier des sockets (Connection et reception de donnée)
 *
 */
void *
CServerTcpIP_Runtime (void *pdata)
{
	CServerTcpIP *this = (CServerTcpIP *)pdata;
	char buffer_rx[CSERVERTCPIP_RX_BUFFER_SIZE];
	int i, ret, readed;
	int pfd_size;
	struct pollfd *pfd = NULL;
	Client *curseur;
	Client *welcome;
	struct sockaddr_in addr_client;			/* Information sur un client lors de sa connection */
	unsigned int size_addr_client = sizeof(struct sockaddr_in);
	int flag;								/* flag de sortie de boucle */

	/*
	 *	Configuration du thread
	 */
	pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype (PTHREAD_CANCEL_DEFERRED, NULL);

	/*
	 *	Boucle d'écoute
	 */
	while (1)
	{
		/*
		 *	Allocation et configuration du tableau de "struct pollfd" pour le poll
		 */
		if (pfd != (struct pollfd *) NULL)
			free (pfd);

		pfd_size = 1 + this->m_iClientNumber;
		pfd = (struct pollfd *) malloc (pfd_size * sizeof (struct pollfd));
		if (pfd == (struct pollfd *) NULL) {
			DEBUG ("Erreur critique sur l'allocation de pfd\n");
			continue;
		}

		pfd[0].fd = this->m_fdListen;
		pfd[0].events = POLLIN | POLLPRI;

		curseur = this->m_clistClients;
		for (i=0; i<(pfd_size-1); i++) {
			pfd[i+1].fd = curseur->fd;
			pfd[i+1].events = POLLIN | POLLPRI;	
			curseur = curseur->next;
		}

		/*
		 *	Attente d'evenement sur les descripteurs de fichier
		 */
		flag = 0;
		do {
			pthread_testcancel ();
			ret = poll (pfd, pfd_size, 5000 /* ms */);
			if (ret < 0) {
				/* Erreur sur le poll */
				DEBUG ("Erreur sur le poll\n");
			} else if (ret == 0) {
				/* Time-out */
				DEBUG_FLOOD ("Timeout !\n");
			} else {
				/* Un moins 1 fd est pret ! */
				for (i=0; i<pfd_size; i++) {
					if (pfd[i].revents & (POLLIN | POLLPRI)) {
						if (i == 0) {
							/* Socket d'ecoute : c'est une demande de connection */
							welcome = (Client *) malloc (sizeof (Client));
							welcome->fd = accept(this->m_fdListen, (struct sockaddr *) &addr_client, &size_addr_client);
							if (welcome->fd < 0) {
								/* Erreur lors de l'accept */
								free (welcome);
							} else {
								/* Connection ok */
								welcome->next = NULL;
								welcome->adresseIP = strdup(inet_ntoa(addr_client.sin_addr));
								welcome->port = ntohs(addr_client.sin_port);
								DEBUG_INFO ("Connection de IP=%s:%d\n", welcome->adresseIP, welcome->port);
								CServerTcpIP_AddClient (this, welcome);
								if (this->m_connect_callback != (CServerTcpIP_connect_t) NULL) {
									this->m_connect_callback (this, welcome, this->m_pvPrivateData);
								}
								flag = 1;
							}
						} else {
							/* Socket client : Donnée disponible en lecture */
							memset (buffer_rx, '\0', CSERVERTCPIP_RX_BUFFER_SIZE);
							readed = recv (pfd[i].fd, buffer_rx, CSERVERTCPIP_RX_BUFFER_SIZE, 0);
							if (readed <= 0) {
								/* Erreur (-1) ou fin de connection (0) */
								CServerTcpIP_DelClient (this, CServerTcpIP_FindClient (this, pfd[i].fd));
								flag = 1;
							} else {
								if (this->m_callback != (CServerTcpIP_rx_t) NULL) {
									Client *tmp = CServerTcpIP_FindClient (this, pfd[i].fd);
									this->m_callback (buffer_rx, readed, this, tmp, this->m_pvPrivateData);
								}
							}
						}
					} else if (pfd[i].revents & (POLLERR | POLLHUP)) {
						/* Connection IP perdu */
						CServerTcpIP_DelClient (this, CServerTcpIP_FindClient (this, pfd[i].fd));
						flag = 1;
					}
				}
			}
		} while (!flag);
	}
}



static int
secure_send (int fd, char *buffer, unsigned int buffer_size, int flags)
{
	int ret, offset, size;
	struct pollfd pfd;
	
	/* Init */
	size = buffer_size;
	offset = 0;
	pfd.fd = fd;
	pfd.events = POLLOUT;
	
	do {
		ret = poll (&pfd, 1, 1000);
		if (ret <= 0) {
			/* Error or timeout */
			return -EIO;
		}
	
		DEBUG ("fd=%d, buffer+offset=%d, size=%d\n", fd, (int)(buffer + offset), size);
		ret = send (fd, buffer + offset, size, flags);
		if (ret < 0) {
			perror (NULL);
			return ret;
		}
			
		offset += ret;
		size -= ret;
		if (size <= 0)
			return buffer_size;
			
	} while (1);
}



/*
 *	Fonction : Send
 *	Description : 	Envoie un message au client représenté par "destinataire" ou bien a tous les clients si destinataire vaut NULL
 *			Une erreur sur l'envoie provoque la fermeture du socket client, la connection est perdu
 */
static int
CServerTcpIP_Send (CServerTcpIP* this, Client *to, char *buffer, unsigned int buffer_size)
{
	int ret;
	
	/*
	 *	Broadcast
	 */
	if (to == (Client *) NULL){
		to = this->m_clistClients;
		while (to != (Client *) NULL) {
			DEBUG ("Destinataire : %p, fd = %d\n", to, to->fd);
			if (secure_send (to->fd, buffer, buffer_size, MSG_DONTWAIT) != buffer_size) {
				DEBUG ("Erreur lors de l'envoie d'un message au client %s:%d\n", to->adresseIP, to->port);
				close (to->fd);
				CServerTcpIP_DelClient (this, to);
			}

			to = to->next;
		}

		return buffer_size;
	} else {
		/*
		 *	Unicast
		 */
		ret = secure_send (to->fd, buffer, buffer_size, MSG_DONTWAIT);
		if (ret != buffer_size) {
			DEBUG ("Ret = %d, buffer_size = %d\n", ret, buffer_size);
			DEBUG ("Erreur lors de l'envoie d'un message au client %s:%d\n", to->adresseIP, to->port);
			close (to->fd);
			CServerTcpIP_DelClient (this, to);
			return -1;
		}
	}
	return buffer_size;
}

static int
CServerTcpIP_Start (CServerTcpIP *this, unsigned short port)
{
	int ret;
	int sock_opt;
	struct sockaddr_in addr_serv;
	
	/* Check for multiple call */
	if (this->m_fdListen >= 0) {
		DEBUG ("Listen socket already opened.\n");
		return 0;
	}

	/* Init a socket */
	this->m_fdListen = socket (PF_INET, SOCK_STREAM, 0);
	if (this->m_fdListen < 0) {
		DEBUG ("Can't create the socket\n");
		return -1;
	}


	/*	Configure SO_REUSEADDR
	 *	if sock_opt = 1 - Allow bind to re-use a sowket in TIME_WAIT mode
	 *	if sock_opt = 0 - Doesn't
	 */
	sock_opt = 1;	
	ret = setsockopt (this->m_fdListen, SOL_SOCKET, SO_REUSEADDR, (void *) &sock_opt, sizeof (sock_opt));
	if (ret != 0) {
		DEBUG ("Error on setsockopt with command SO_REUSEADDR and arg=%d\n", sock_opt);
	}

	/* Bind */
	addr_serv.sin_family = AF_INET;	 
   	addr_serv.sin_addr.s_addr = htonl(INADDR_ANY);
   	addr_serv.sin_port = htons (port);
   	ret = bind (this->m_fdListen, (struct sockaddr *) &addr_serv, sizeof(addr_serv));
	if (ret < 0) {
		DEBUG ("Error on bind\n");
		goto socket_error;
	}

	/* Listen */	
	ret = listen (this->m_fdListen, CSERVERTCPIP_LISTEN_QUEUE_SIZE);
	if (ret < 0) {
		DEBUG ("Error on listen\n");
		goto socket_error;
	}

	/* Run listen thread */
	ret = pthread_create(&(this->m_threadListen), NULL, CServerTcpIP_Runtime, this);
	if (ret != 0) {
		DEBUG ("Error on pthread_create\n");
		goto thread_error;
	}

	return 0;

thread_error:
	shutdown (this->m_fdListen, SHUT_RDWR);
	
socket_error:
	close (this->m_fdListen);
	this->m_fdListen = -1;
	return -1;
}

static void
CServerTcpIP_Free (CServerTcpIP *this)
{
	/* Close all conections */
	CServerTcpIP_Stop (this);

	free (this);
}

CServerTcpIP *
CServerTcpIP_New (CServerTcpIP_rx_t rx_callback, CServerTcpIP_connect_t connect_callback, void *pdata)
{
	/* Memory allocation */
	CServerTcpIP *this = (CServerTcpIP *) malloc (sizeof (CServerTcpIP));
	if (this == (CServerTcpIP *) NULL)
		return NULL;

	/* Save data */
	this->m_callback = rx_callback;
	this->m_connect_callback = connect_callback;
	this->m_pvPrivateData = pdata;

	/* Methods connection */
	this->Free = CServerTcpIP_Free;
	this->Send = CServerTcpIP_Send;
	this->GetNbClientsConnected = CServerTcpIP_GetNbClientsConnected;
	this->Start = CServerTcpIP_Start;
	this->Stop = CServerTcpIP_Stop;

	/* Init */
	this->m_clistClients = NULL;
	this->m_iClientNumber = 0;
	this->m_fdListen = -1;

	return this;
}

