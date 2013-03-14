#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include "libcan.h"
#include "CServerTcpIP.h"
#include "debug.h"


char * can_iface_ptr = "can0";	
int serveur_running;
CServerTcpIP *this = NULL;

char fileName[256];


/*
 * crée un fichier si celui-ci n'existe pas
 */
void initXML(){

	FILE* xml = NULL;
	if(fopen((const char *)fileName, "r")==NULL){	//si le fichier n'existe pas
		xml = fopen((const char *)fileName, "a");	//le créer
		if (xml != NULL){
			fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", xml);
			fprintf(xml, "<%s>",can_iface_ptr);
			fprintf(xml, "</%s>",can_iface_ptr);
		}
		else{
			printf("Echec d'écriture du fichier xml");
		}
		fclose(xml);
	}
}

/*
 * Génère un timestamp a la milliseconde
 */
char *timestamp(){
	struct timeval tv;
	char *timestamp = malloc (sizeof (*timestamp) * 16);
	gettimeofday(&tv,NULL);
	sprintf(timestamp,"%d%d",tv.tv_sec, tv.tv_usec/1000);
	return timestamp;
}



/*
 * Fuction called back when TCP/IP Server received data
 */
void protocole (char *buffer, unsigned int buffer_size, CServerTcpIP *this, Client *expediteur, void *private_data){
	DEBUG_INFO ("Client %s:%d\n", expediteur->adresseIP, expediteur->port);
	DEBUG_INFO ("%d octets recu : %s\n", buffer_size, buffer);

	/* Echo */
	//this->Send (this, expediteur, buffer, buffer_size);
	//printf("Date : %d\n", timestamp());

	if (strncmp ("enregistrer", buffer, 11) == 0) {
		
		
		char	*separateur = "-\n";     //séparateurs
		char    *Chaine_Entrante;
		char 	*nom;
		
		nom = malloc (sizeof (*nom) * 256);

		Chaine_Entrante = strdup(buffer);	// /!\ génere une malloc
		nom = strtok(Chaine_Entrante, separateur);
		nom = strtok(NULL, separateur);
		strncpy(fileName, nom, sizeof(fileName));
		fileName[sizeof(fileName) - 1] = '\0';
		printf("%s\n",fileName);		
		initXML();

		free(Chaine_Entrante);

		/* Initialisation CAN*/
		void dump(CServerTcpIP *this, struct can_frame cf);
		//if(can_isok() == 0){
			if(can_init(can_iface_ptr)){
				printf("Il y a eu un erreur a l'init du CAN\n");
				exit(1);
			}
		//}
		if(can_bind_receive(0x000, 0x000, NULL, 0, dump)){
			fprintf(stderr, "Erreur au bind de reception\n");
		}
	}

	if (strncmp ("cansend", buffer, 7) == 0) {
		
		
		char	*separateur = " #\n";     //séparateurs
		char    *Chaine_Entrante;
		char 	*trame;
		struct 	can_frame msg;
		int 	i, j;
		char	data[3];
		
		j =0;
		trame = malloc (sizeof (*trame) * 256);
		
		Chaine_Entrante = strdup(buffer);	// /!\ génere une malloc
		trame = strtok(Chaine_Entrante, separateur);
		if(trame != NULL){
			trame = strtok(NULL, separateur);
			printf("%s\n",trame);	
			msg.can_id = (int)strtol(trame, NULL, 16);
		}
		
		if(trame != NULL){
			trame = strtok(NULL, separateur);
			for(i=0;i<strlen(trame);i+=2){
				sprintf(data, "%c%c", trame[i], trame[i+1]);
				msg.data[j] = (int)strtol(data, NULL, 16);				
				printf("%s\n",data);
				j++;
			}
		}
		
		msg.can_dlc = (strlen(trame)-1)/2+(strlen(trame)-1)%2;	

		free(Chaine_Entrante);
		if(can_isok() == 0){
			if(can_init(can_iface_ptr)){
				printf("Il y a eu un erreur a l'init du CAN\n");
				exit(1);
			}
		}
		can_send (msg);
			
	}	
	if (strncmp ("stop", buffer, 4) == 0) {
		can_close();
	}	

	/* Quit if receive 'exit' from a client */
	if (strncmp ("exit", buffer, 4) == 0) {
		this->Send (this, expediteur, "Bye bye !\n", sizeof ("Bye bye !\n") -1);
		serveur_running = 0;
	}
}

void onConnect (CServerTcpIP *this, Client *from, void *pdata){
	DEBUG_INFO ("New client %s:%d\n", from->adresseIP, from->port);
}



/*
 *	Mettre a jour le fichier can.xml
 */

void ecrireXML(char *trame){
	FILE* xml = NULL;
	xml = fopen((const char *)fileName, "r+");		
	if (xml != NULL){
		/* Permet de sauvegerder la fermeture de la balise racine*/

		/*repositionne le pointeur av la balise racine ex(</can0>)*/
		fseek(xml, -strlen(can_iface_ptr)-3, SEEK_END); 
		
		/*sauvegarde des données*/
		fprintf(xml, "%s</%s>", trame, can_iface_ptr);
	}
	else{
		printf("Echec d'écriture du fichier xml");
	}
	fclose(xml);
}

/*
 * Affiche la trame CAN de maniere lisible sur le serveur
 */

void afficheTrame(struct can_frame cf){
	int i = 0;	
	printf("id=\"0x%X\" dlc=\"%d\" data=\"", cf.can_id, cf.can_dlc);
	for(i = 0;i < cf.can_dlc;i++){
		printf("%X ",cf.data[i]);
	}
}

/*
 * Fonction de callback appeler lors de la récéption d'une trame CAN
 */

void dump(CServerTcpIP *this, struct can_frame cf){
	int i = 0;
	char *trame = malloc (sizeof (*trame) * 2048);
	char *temp = malloc (sizeof (*temp) * 2048);	
	
		
	//sprintf(trame, "reception CAN ok \n");	
	sprintf(trame, "<trame><id>0x%X</id><dlc>%d</dlc><timestamp>%s</timestamp><data>", cf.can_id, cf.can_dlc, timestamp());
	//sprintf(trame, "<trame><id>0x%X</id><dlc>%d</dlc><timestamp>0</timestamp><data>", cf.can_id, cf.can_dlc);
	//Formatage des données	
	for(i = 0;i < cf.can_dlc;i++){
		sprintf(temp, "<data%d>0x%X</data%d>", i, cf.data[i], i);
		strcat(trame, temp);
		memset (temp, 0, sizeof (temp));
	}
	sprintf(temp, "</data></trame>");
	strcat(trame, temp);
	memset (temp, 0, sizeof (temp));
	
	ecrireXML(trame);
	//prologue et rajout des balise racines
	sprintf(temp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><%s>%s</%s>\n", can_iface_ptr, trame, can_iface_ptr); 
	strcpy(trame, temp);
	//printf("%s\n\n\n",trame);
	this->Send (this, NULL, trame, strlen(trame));
	free(temp);
	free(trame);
	//this->Send (this, NULL, "reception CAN ok \n", sizeof ("reception CAN ok \n")-1);
}

void sigterm(int signo)
{
#ifdef DEBUG
	fprintf(stderr, "Signal recu : %d\n", signo);
#else
	signo = signo;
#endif
	serveur_running = 0;
}

int main(int argc, char * argv[]){
	signal(SIGTERM, sigterm);	//Fin de processus
	signal(SIGHUP, sigterm);	//Fin de connection
	signal(SIGINT, sigterm); 	//Ctrl-C

	

	/* Initialisation Serveur TCP*/
	int ret;
	DEBUG_INFO ("Server is listening on port 1234.\nIf a client say \"exit\", he will stop the server.\n");

	/*
	 *	Create a TCP/IP server which listen on port 1234
	 *	All data receive from peer is send to the callback, here the function 'protocole'
	 *	A private data can be pass to the server, and will be automatically pass to the callback; not use here !
	 */
	this = CServerTcpIP_New (protocole, onConnect, NULL);
	if (this == (CServerTcpIP *) NULL) {
		DEBUG ("Can't create CServerTcpIP object\n");
		return 0;
	}
	
	/*
	 *	Start the listen socket on port 1234
	 */
	ret = this->Start (this, 1234);
	if (ret != 0) {
		DEBUG ("Can't start the listen socket\n");
		return 0;
	}
	
	//this->Send (this, NULL, "Connection OK !\n", sizeof ("Connection OK !\n") -1);
	
	
	
	serveur_running = 1;
	while(serveur_running){
		sleep(5);
		//printf("Hello World\n");
		//this->Send (this, NULL, "Connection OK !\n", sizeof ("Connection OK !\n") -1);
	}
	/* 
	 * Ferme le socket can si il est ouvert
	 */	
	if(can_isok() == 1){
		can_close();
	}
	this->Free (this);
	//free(nom);

	return 0;

}
