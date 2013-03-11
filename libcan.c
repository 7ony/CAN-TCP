/**
 * @file libcan.c
 *
 * @brief Encapsule tout le bas niveau CAN dans des fonctions plus
 * faciles à utiliser.
 */



#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <net/if.h>

#include "libcan.h"
#include "CServerTcpIP.h"

/** @brief  File descriptor du Socket CAN */
static int socket_can;

/** @brief File descriptor de la fifo en lecture */
static int fifofd_rd;
/** @brief File descriptor de la fifo en écriture */
static int fifofd_wr;
/** @brief Constante sensée être dans les headers systèmes : linux, libc, etc...*/
#define AF_CAN 29
/** @brief Constante sensée être dans les headers systèmes : linux, libc, etc...*/
#define PF_CAN 29
/** @brief Nom du fichier contenant la fifo générée aléatoirement */
static char fifo[L_tmpnam] = {0,};

/** @brief  Variable d'état de la lib : 1=>OK; 0=>KO */
static int can_ok=0;


/** @brief Thread principal de la lib */
static pthread_t can_thread;

/** @brief Variable permettant à la lib de s'arreter proprement : 1=OK, 0=STOP! */
static int volatile continu = 1;
/**
* @brief Binds de reception : Association d'un identifiant+masque à une zone
* 	mémoire et à un calback
*/
struct bind_rx
{
    unsigned short id; 		/*!< Identifiant CAN*/
    unsigned short mask;	/*!< Masque */
    void * pmem;		/*!< Zone mémoire à remplir. Ignorée si NULL */
    unsigned char len;		/*!< Longueur de la zone mémoire.*/
    void (*callback)
	(CServerTcpIP *this, struct can_frame cf);	/*!< callback à appeler lors d'un match du message. Ignoré si NULL */
};

/** @brief Nombre maximum de binds actif en reception */
#define MAX_RX_BINDS 10
/** @brief Tableau des binds en reception */
static struct bind_rx binds_rx[MAX_RX_BINDS];
/** @brief Pointeur sur l'élément du tableau de bind pas encore rempli */
static struct bind_rx * ptr_bind_rx_max = binds_rx;


/**
* @brief Vérifie les binds sur reception d'un message CAN
*
* can_rx est appelée lors de la reception d'un message.
* Elle cherche alors un bind qui correspond au message reçu.
* Si elle en trouve, alors elle rempli (si cela à lieu d'être) la zone mémoire
* et appelle le callback du bind.
*
* @param cf Le message reçu
*/
static void can_rx(struct can_frame cf);

/** @brief Calcule le minimum entre a et b */
#define MIN(a,b) (((a)<(b))? (a) : (b))
/** @brief Calcule le maximum entre a et b */
#define MAX(a,b) (((a)>(b))? (a) : (b))

/**
* @brief Binds d'émission : Association d'un identifiant+periode à une zone
* 	mémoire.
*/
struct bind_tx
{
    unsigned short id;		/*!< Identifiant CAN*/
    void * pmem;		/*!< Zone mémoire à envoyer */
    unsigned short len;		/*!< Longueur de la zone mémoire.*/
    unsigned long period;	/*!< Période d'envoi en ms */
    struct timeval last_send;	/*!< Date du dernier envoi du message */
};

/** @brief Nombre maximum de binds d'émission */
#define MAX_TX_BINDS 10
/** @brief  Tableau des binds d'émission */
static struct bind_tx binds_tx[MAX_TX_BINDS];
/** @brief Pointeur sur l'élément du tableau de bind pas encore rempli */
static struct bind_tx * ptr_bind_tx_max = binds_tx;

/* @brief Temps en ms d'avance du déclanchement  d'un envoi avant son échance réel
 * Permet d'envoyer des messages juste avant leurs échéances au lieu de 10ms
 * trop tard */
#define DELTA_TIME 3

/**
* @brief Cherche un message à envoyer parmis les binds
*
* can_tx_periodique est appelée périodiquement et est chargée de trouver parmis les
* binds actifs celui qui a dépassé sa période et d'envoyer le message
* correspondant.
*
* @param signo Signal appelant la fonction (SIGALRM ici)
*/
static void can_tx_periodique(int signo);


/**
* @brief Thread principal : Traite les messages reçus pour et depuis le CAN.
*
* Attend sur le socket CAN et sur la fifo d'envois de message. Dès qu'un message
* se présente il le traite.
*/
static void * can_thread_fct(void* args)
{
    fd_set readfds;
    struct timeval timeout;
    int ndfs;
    struct can_frame msg;
    args = args;

    /* Initializing select() */
    while(continu)
    {

	FD_ZERO(&readfds);
	FD_SET(socket_can, &readfds);
	FD_SET(fifofd_rd, &readfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 100000;

	ndfs = MAX(socket_can, fifofd_rd) + 1;

	while(select(ndfs, &readfds, NULL, NULL, &timeout) > 0) /* Boucle de 100 ms */
	{
	    if(FD_ISSET(socket_can, &readfds))
	    {
		/* Données reçues sur le CAN */
		int err;

		/* Lecture des donneés */
		if(!(err = recv(socket_can, &msg, sizeof(msg), 0)) == sizeof(msg))
		{
		    if(err == -1) perror("Recv from socket can");
		    else fprintf(stderr, "Incomplete read from socket can");
		}
		/* Traitement du message reçu */
		can_rx(msg);
	    }

	    if(FD_ISSET(fifofd_rd, &readfds))
	    {
		/* Données reçues dans la FIFO d'envoi */
		int err;

		/* Lecture des données dans la Fifo*/
		if ((err=read(fifofd_rd, &msg, sizeof(msg))) == sizeof(msg))
		{
		    /* Envoi de la donnée par le socket CAN */
		    if(!(err = write(socket_can, &msg, sizeof(msg))) == sizeof(msg))
		    {
			if(err== -1)
			    perror("Writing on socket can");
			else
			    fprintf(stderr, "Incomplete Write on socket can");
		    }
		}
		else
		{
		    if(err == -1)
		    {
			if(errno != EAGAIN) /* EAGAIN si la fifo est vide */
			    perror("Read from fifo");
		    }
		    else
		    {
			if(!err == 0)
			    fprintf(stderr, "Incomplete read from fifo");
		    }
		}

	    }

	    /* Réinitialisation de select() */

	    FD_ZERO(&readfds);
	    FD_SET(socket_can, &readfds);
	    FD_SET(fifofd_rd, &readfds);
	    ndfs = MAX(socket_can, fifofd_rd) + 1;
	}
    }

    pthread_exit(NULL);

}


/**
* @brief Initialise la lib_can
*
* Ouvre un socket can en lecture écriture. Crée le thread principal de la
* lib_can. Crée une fifo pour la communication entre le programme appelant et la
* lib_can. Crée un timer pour l'appel périodique à la fonction d'envoi.
* Si la libcan est déjà active, alors il ne se passera rien.
*
* @param iface_can chaine pour l'interface CAN (ex : "can0")
*
* @returns 0 si OK, 1 si socket_can KO, 2 FIFO, 3 Thread can, 4 Timer, 5 libcan active
*/
int can_init(const char * iface_can)
{
   /* Socket CAN */
    struct ifreq ifr;
    struct sockaddr_can addr;

   /* Alarme périodique */
    struct itimerval interval;

    if(!can_ok)
    {
	/* Socket CAN */
	if ((socket_can = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = AF_CAN;

	if(iface_can == NULL)
	{
	    fprintf(stderr, "Utilisation de \"can0\" comme interface can par defaut\n");
	    strcpy(ifr.ifr_name, "can0");
	}
	else strcpy(ifr.ifr_name, iface_can);

	if (ioctl(socket_can, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		return 1;
	}


	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(socket_can, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* FIFO */
	while ((tmpnam(fifo)) != NULL){
	    if (mkfifo(fifo, 0600) == 0)
		break;
	    if (errno != EEXIST)
	    {
		perror("Fifo");
		return 2;
	    }
	}
	if (fifo == NULL) return 2;

	if ((fifofd_rd = open(fifo, O_RDONLY | O_NONBLOCK)) < 0) {
	    perror("open error");
	    return 2;
	}
	if ((fifofd_wr= open(fifo, O_WRONLY)) < 0) {
	    perror("open(myfunc) error");
	    return 2;
	}

	/* Lancement du thread */
	if (pthread_create(&can_thread, NULL, can_thread_fct, (void *) NULL)) {
	    perror("erreur pthread");
	    return 3;
	}

	/* Association signal de l'alarme à la fonction can_tx_periodique */
	signal(SIGALRM, can_tx_periodique);

	/* Création de l'alarme périodique : Le process va recevoir des signaux
	 * SIGALRM à intervalles réguliers */
	interval.it_interval.tv_sec = 0;
	interval.it_interval.tv_usec = 10000;
	interval.it_value.tv_sec = 0;
	interval.it_value.tv_usec = 10000;

	if(setitimer(ITIMER_REAL,& interval, NULL))
	{
	    perror("setitimer");
	    return 4;
	}

	can_ok = 1;
    }
    else return 5;

    return 0;
}


/**
* @brief Termine la lib_can
*
* Attend la fin du thread. Ferme les fifos et le socket, supprime la fifo
* utilisée.
*
* @returns 0
*/
int can_close(void)
{

	continu = 0;
	pthread_join(can_thread, NULL);
	/* unlike fifo*/
	close(socket_can);
	close(fifofd_rd);
	close(fifofd_wr);

#ifdef DEBUG
	printf("Fifo supprimee : %s\n", fifo);
#endif
	/* Suppression de la fifo temporaire */
	unlink(fifo);
	can_ok = 0;
	return 0;
}





/**
* @brief Donne l'état de la lib_can.
*
* @returns 1 si la lib est lancée, 0 sinon.
*/
int can_isok()
{
    return can_ok;
}


/**
* @brief Envoi directement un message
*
* @param msg message CAN à envoyer
*
* @returns 0 si OK, 1 si erreur
*/
int can_send(struct can_frame msg)
{
	int nbytes;

	/* send frame */
	if ((nbytes = write(fifofd_wr, &msg, sizeof(msg))) != sizeof(msg)) {
		perror("Write on fifo");
		return 1;
	}

	return 0;
}


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
int can_bind_send(unsigned short ID, void * zone, unsigned short zone_length, unsigned long period)
{
    if((ptr_bind_tx_max - binds_tx) < MAX_TX_BINDS)
    {
	/* Remplissage structure Bind */
	if(!(ID<=0x7FF)) return 1;
	ptr_bind_tx_max->id = ID;

	if(zone_length > 0) ptr_bind_tx_max->pmem = zone;
	else ptr_bind_tx_max->pmem = NULL;

	ptr_bind_tx_max->len = zone_length;
	if(period==0) return 2;
	ptr_bind_tx_max->period = period;

	gettimeofday(&( ptr_bind_tx_max->last_send), NULL); /* Init du temps */

	ptr_bind_tx_max++;
    }
    else
    {
	fprintf(stderr,  "lib_can : Trop de binds: augmenter MAX_TX_BINDS\n");
	return 3;
    }
    return 0;
}


/**
* @brief Cherche un message à envoyer parmis les binds
*
* can_tx_periodique est appelée périodiquement et est chargée de trouver parmis les
* binds actifs celui qui a dépassé sa période et d'envoyer le message
* correspondant.
*
* @param signo Signal appelant la fonction (SIGALRM ici)
*/
static void can_tx_periodique(int signo)
{
    struct timeval now;
    struct bind_tx * ptr_bind;
    struct can_frame cf;

    if(signo != SIGALRM)
	return;
    gettimeofday(&now, NULL);

#ifdef DEBUG
    printf("TIC_TX %ld %ld\n", now.tv_sec, now.tv_usec);
    printf("Recherche Bind_TX actif : ID, ptr_mem, longueur, periode\n");
#endif
    /* Recherche Bind d'envoi actif */
    for(ptr_bind = binds_tx; ptr_bind < ptr_bind_tx_max; ptr_bind++)
    {
#ifdef DEBUG
	printf("Bind_Tx : %#x, %p, %d, %ld\n", ptr_bind->id, ptr_bind->pmem,
	        ptr_bind->len, ptr_bind->period);
#endif
	if( (unsigned)(1000*(now.tv_sec - ptr_bind->last_send.tv_sec)
	    + (now.tv_usec - ptr_bind->last_send.tv_usec)/1000)
	    >=  (ptr_bind->period - DELTA_TIME))
	{
	    /* Trouvé*/
#ifdef DEBUG
	    printf("MATCH_TX! %#x %ld %ld\n", ptr_bind->id,
		    now.tv_sec, now.tv_usec);
#endif

	    /* Envoi message */

	    cf.can_id = ptr_bind->id;
	    cf.can_dlc = ptr_bind->len;
	    memcpy(cf.data, ptr_bind->pmem, ptr_bind->len);

	    can_send(cf);
	    memcpy(&(ptr_bind->last_send), &now, sizeof(struct timeval));
	}
    }
}


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
int can_bind_receive(unsigned short ID, unsigned short mask,
    void * zone, unsigned short zone_length, void (*callback)(CServerTcpIP *this, struct can_frame cf))
{
    if((ptr_bind_rx_max - binds_rx)<MAX_RX_BINDS)
    {
	/* Remplissage structure Bind */
	if(!(ID<=0x7FF)) return 1;
	ptr_bind_rx_max->id = ID;

	if(!(mask<=0x7FF)) return 2;
	ptr_bind_rx_max->mask = mask;

	if(zone_length > 0) ptr_bind_rx_max->pmem = zone;
	else ptr_bind_rx_max->pmem = NULL;

	ptr_bind_rx_max->len = zone_length;

	ptr_bind_rx_max->callback = callback;
	ptr_bind_rx_max++;
    }
    else
    {
	fprintf(stderr,  "lib_can : Trop de binds : augmenter MAX_RX_BINDS\n");
	return 3;
    }
    return 0;
}


/**
* @brief Vérifie les binds sur reception d'un message CAN
*
* can_rx est appelée lors de la reception d'un message.
* Elle cherche alors un bind qui correspond au message reçu.
* Si elle en trouve, alors elle rempli (si cela à lieu d'être) la zone mémoire
* et appelle le callback du bind.
*
* @param cf Le message reçu
*/
static void can_rx(struct can_frame cf)
{
#ifdef DEBUG
    int i;
    printf("R%3x %d ", cf.can_id, cf.can_dlc);
    for (i = 0; i<cf.can_dlc; i++)
    {
	printf("%02x ", cf.data[i]);
    }
    printf("\n");
#endif /* DEBUG */

    struct bind_rx * ptr_bind;

#ifdef DEBUG
    printf("Recherche Bind_RX actif : ID, mask, ptr_mem, longueur, callback\n");
#endif

    for(ptr_bind = binds_rx; ptr_bind < ptr_bind_rx_max; ptr_bind++)
    {
#ifdef DEBUG
	printf("Bind_Rx : %#x, %#x, %p, %#x, %p\n", ptr_bind->id,
 		ptr_bind->mask, ptr_bind->pmem, ptr_bind->len, ptr_bind->callback);
#endif
	/* Recherche de Bind correspondant au message reçu */
	if( (cf.can_id & ptr_bind->mask) == (ptr_bind->id & ptr_bind->mask))
	{
#ifdef DEBUG
	    printf("MATCH_RX!\n");
#endif
	    /* Match*/
	    /* Remplissage mémoire */
	    if(ptr_bind->pmem != NULL)
		memcpy(ptr_bind->pmem, cf.data, MIN(ptr_bind->len, cf.can_dlc));

	    /* Appel callback */
	    if(ptr_bind->callback != NULL)
		ptr_bind->callback(this, cf);
	}
    }
}

