#include <stdio.h>
#include <errno.h> 
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <linux/net.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <uci.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#define SOCKET_IO_REV   "0.1"
#define HW_VER "0.1"	

#define TIMEOUT	100000L   	/* in us */
#define SOCKET_BUFLEN 1500	/* One standard MTU unit size */ 
#define PORT 9930
#define OUTPUTS_NUM	4		/* We have that many gpio outputs */
#define INPUTS_NUM 	4       /* We have that many gpio inputs */
#define STR_MAX		100		/* Maximum string length */
#define MSG_MAX     500     /* Maximum UDP message length */
#define UDP_ARGS_MAX 20		/* we can have that much arguments ('/' separated) on the UDP datagram */ 

#define REL0	16			/* GPIOs controlling the outputs */
#define REL1    28			/* The relay address is [REL0 REL1]  so REL1 is LSB */
#define S_R		15
#define PULSE   1

#define FB0		18			/* GPIO Feedbacks from the relay outputs*/
#define FB1     21
#define FB2     22
#define FB3     27

#define IN0     19          /* GPIO Inputs */
#define IN1     20
#define IN2     23
#define IN3     24

char SIOD_ID[STR_MAX];		/* Our SIOD ID */

unsigned char GPIOs;		/* We keep the status of the 8 IOs in this byte   */
							/*	GPIOS = [IN3 IN2 IN1 IN0 OUT3 OUT2 OUT1 OUT0] */
							/*			 MSB							LSB   */ 

int strfind(const char *s1, const char *s2);
int process_udp(char *datagram);
void RemoveSpaces(char* source);
int extract_args(char *datagram, char *args[], int *n_args);
char *strupr(char *s);
unsigned long long MACaddress_str2num(char *MACaddress);
void MACaddress_num2str(unsigned long long MACaddress, char *MACaddress_str);
unsigned long IPaddress_str2num(char *IPaddress);
void IPaddress_num2str(unsigned long IPaddress, char *IPaddress_str);
unsigned long long eth0MAC(void);
unsigned long long eth1MAC(void);
unsigned long long wifiMAC(void);
int uciget(const char *param, char *value);
int uciset(const char *param, const char *value);
int ucidelete(const char *param);
int uciadd_list(const char *param, const char *value);
void ucicommit(void);
void restartnet(void);
void uptime(char *uptime);
int getsoftwarever(char *ver);
int broadcast(char *msg);
int unicast(char *msg);
int gpios_init(void);
int setgpio(char *X, char *Y);
int getgpio(char *X, char *Y);
void intHandler(int dummy);

enum 		   {ConfigBatmanReq, ConfigBatmanRes, ConfigBatman, ConfigReq, ConfigRes, Config, \
	  			RestartNetworkService, RestartAsterisk, ConfigAsterisk, AsteriskStatReq, AsteriskStatRes, \
	  			ConfigNTP, Set, SetIf, TimeRange, TimeRangeOut, Get, Put, GSTCheckSumReq, GSTCheckSum, \
	  			GSTReq, GSTdata, Ping, PingRes};
char *cmds[26]={"ConfigBatmanReq", "ConfigBatmanRes", "ConfigBatman", "ConfigReq", "ConfigRes", "Config", \
      			"RestartNetworkService", "RestartAsterisk", "ConfigAsterisk", "AsteriskStatReq", "AsteriskStatRes", \
      			"ConfigNTP", "Set", "SetIf", "TimeRange", "TimeRangeOut", "Get", "Put", "GSTCheckSumReq", "GSTCheckSum", \
	  			"GSTReq", "GSTdata", "Ping", "PingRes"};

int verbose=0; 	/* get value from the command line */


/* listening socket */
int udpfd;
struct sockaddr_in servaddr, cliaddr;

/* socket for the brodcasting messages*/
int bcast_sockfd;
struct sockaddr_in bcast_servaddr;


/* global file descriptors so we don't have to open aand close all the time */
int fd_in0, fd_in1, fd_in2, fd_in3, fd_fb0, fd_fb1, fd_fb2, fd_fb3, fd_rel0, fd_rel1, fd_s_r, fd_pulse;
int IOs[OUTPUTS_NUM+INPUTS_NUM];


int main(int argc, char **argv){

	int n, nready; 
	char datagram[SOCKET_BUFLEN];
	fd_set rset;
	socklen_t addrlen;
	struct timeval	timeout;
	int res, enabled;	


	/* CTR-C handler */
	signal(SIGINT, intHandler);


	/* Splash ============================================================ */

	/* Check for verbosity argument */
	if(argc>1) {
		if(!strcmp(argv[1], "-v")){
			verbose=1;
			printf("socket_io - rev %s (verbose)\n", SOCKET_IO_REV);		
		} else if(!strcmp(argv[1], "-vv")){
			verbose=2;
			printf("socket_io - rev %s (very verbose)\n", SOCKET_IO_REV);
		}
	} else
		printf("socket_io - rev %s\n", SOCKET_IO_REV);

	/* Init. local IOs, outputs set as per the previous relay feedbacks == */
	gpios_init();
	
	/* Retreave the local output state from the mesh GST 
       if no data available read the current relays state ================ */
	//TBD
/*
	{
		//Some tests 

		int i, s_r;
		char str1[STR_MAX], str2[STR_MAX];
		i=0;
		s_r=1;
		while(1){
			sprintf(str1, "%d", i++);
			sprintf(str2, "%d", s_r);		
			setgpio(str1, str2);

			if(i==4) {i=0; s_r = 1-s_r;}
			sleep(1);
		} 
	
		printf( "Press [Enter] to set REL0 . . ." ); getchar();
		setgpio("0", "1");

		printf( "Press [Enter] to set REL2 . . ." ); getchar();
		setgpio("2", "1");

		printf( "Press [Enter] to clear REL2 . . ." ); getchar();

		setgpio("2", "0");

		printf( "Press [Enter] to set 0101 . . ." ); getchar();

		setgpio("", "0101");
		
		printf("GPIOs=0x%x\n", GPIOs);
	}

*/

	/* Initialize the broadcasting socket  =============================== */
    bcast_sockfd=socket(AF_INET,SOCK_DGRAM,0);
    enabled = 1;
    setsockopt(bcast_sockfd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
    bzero(&bcast_servaddr,sizeof(servaddr));
    bcast_servaddr.sin_family = AF_INET;
    bcast_servaddr.sin_addr.s_addr=inet_addr("255.255.255.255");
    bcast_servaddr.sin_port=htons(PORT);	

	/* Start UDP Socket server and listening for commands ================ */
	udpfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(udpfd == -1){
                perror("socket() failed");
		exit(-1);
	}

	//enable reception of broadcasting data
	//enabled = 1;
    //setsockopt(udpfd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));

	/* Prepare the address */
	memset(&servaddr, 0, sizeof(servaddr)); 	
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	if(bind(udpfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) == -1){
		perror("bind() failed");
		exit(-1);
	}

	if(verbose) printf("Listening on port %d\n", PORT);	

	for ( ; ; ) {

		/* descritors set prepared */ 
        	FD_ZERO(&rset);
        	FD_SET(udpfd, &rset);

		/* Set the timeout */
		timeout.tv_sec  = 0;
		timeout.tv_usec = TIMEOUT;

		nready = select(udpfd+1, &rset, NULL, NULL, &timeout);
		if (nready < 0) {
			printf("Error or signal\n");
			if (errno == EINTR)
				continue; /* back to for() */
			else {
      				perror("select() failed");
				exit(-1);
			}
		} else if (nready) {
			/* We have data to read */
				
			addrlen=sizeof(cliaddr);
			if((n = recvfrom(udpfd, datagram, SOCKET_BUFLEN, 0, (struct sockaddr *)&cliaddr, &addrlen))<0){
				/* System error */

				perror("recvfrom() failed");
				exit(-1);
			} else if (n>0){
				/* We have got an n byte datagram */
			
				/* reject the our own  broadcast messages */
				//TBD

				datagram[n] = '\0';
				process_udp(datagram);

				/* Send it back */
				//sendto(udpfd, datagram, n, 0, &cliaddr, addrlen);		
	
			} else {
				/* The socket closed, this should not happen with UDP */
				printf("The socket closed ?!?!\n");
			}
			
		} else {
			/* Timeout, expected to happens each 100ms or so */
		}
	}

	return(0);

}

/* 
 * Closes gpio descriptors on CTR-C 
 */
void intHandler(int dummy) {
	if(verbose) printf("Closing all open file descriptors \n");	

	close(fd_in0); close(fd_in1); close(fd_in2); close(fd_in3); 
	close(fd_fb0); close(fd_fb1); close(fd_fb2); close(fd_fb3); 
	close(fd_rel0); close(fd_rel1); close(fd_s_r); close(fd_pulse);

	close(udpfd); close(bcast_sockfd);

	exit(0);
}



/* 
 * process the data coming from the udp socket
 */
int process_udp(char *datagram){
		
	int n_args;
	char *args[UDP_ARGS_MAX], msg[MSG_MAX];

	if(verbose) printf("In process_udp() \n");


	/* We process only datagrams starting with JNTCIT */
	if ((strlen(datagram)<7) || strncmp(datagram, "JNTCIT/", 7)){
		if(verbose) printf("unrelated datagram => %s\n", datagram);
		return 0;
	} else
		datagram = datagram + 7;

	/* UDP pre processing */
	RemoveSpaces(datagram);
	//datagram = strupr(datagram);
	
	/* extract arguments */
	extract_args(datagram, args, &n_args);

	{//Will be delete !!!!!!!!!!!!!!!!!!!!!!!!!!!!!	
	
		int i;
		for(i=0;i<n_args;i++) 
			printf("%s\n", args[i]);

	}


	switch (hashit(args[0])){
		/*
			ConfigBatmanReq

		*/		
		case ConfigBatmanReq:{

				if(verbose) printf("Rcv: ConfigBatmanReq\n");

			}
			break;
        /*
       ConfigBatmanRes/MACAddress/SSID/Encryption/Passphrase/WANbridge
			MACAddress:(WiFi)	0a:ba:ff:10:20:30 (WiFi MAC used as reference)
			SSID:				jntcit
			Encryption:			WPA2
			Passphrase:			S10D
			WANbridge:			True, False

        */  
        case ConfigBatmanRes:{
			/* We may fill our table about the available MACaddresses in the mesh */
			char *MACaddress = args[1];
			
			if(verbose) printf("Rcv: ConfigBatmanRes\n");


            }
            break;
		/*
		ConfigBatman/MACAddress/SSID/Encryption/Passphrase/WANbridge
			MACAddress:(WiFi)(optional)	0a:ba:ff:10:20:30
			SSID:					jntcit
			Encryption:				WPA2
			Passphrase:				S10D
			WANbridge:				True, False	
		*/
        case ConfigBatman:{

				if(verbose) printf("Rcv: ConfigBatman\n");

            }
            break;
		/*
		ConfigReq

		We send our network parameters, check ConfigRes
		*/
        case ConfigReq:{
				char MACAddressWiFi[STR_MAX], MACAddressWAN[STR_MAX], Uptime[STR_MAX], SoftwareVersion[STR_MAX];
				char IPAddressWiFi[STR_MAX], IPMaskWiFi[STR_MAX], IPAddressWAN[STR_MAX], IPMaskWAN[STR_MAX], Gateway[STR_MAX], DNS1[STR_MAX], DNS2[STR_MAX], DHCP[STR_MAX];

				if(verbose) printf("Rcv: ConfigReq\n");

				MACaddress_num2str(wifiMAC(), MACAddressWiFi);
				MACaddress_num2str(eth1MAC(), MACAddressWAN);
            	uptime(Uptime);
            	getsoftwarever(SoftwareVersion);
				uciget("network.mesh_0.ipaddr", IPAddressWiFi);
            	uciget("network.mesh_0.netmask", IPMaskWiFi);
            	uciget("network.wan.ipaddr", IPAddressWAN);
            	uciget("network.wan.netmask", IPMaskWAN);
            	uciget("network.mesh_0.gateway", Gateway);
            	uciget("network.mesh_0.dns", DNS1);
				DNS2[0]='\0'; //Don't support for now
				uciget("network.mesh_0.proto", DHCP);
				
				sprintf(msg, "JNTCIT/ConfigRes/%s/%s/%s/SIOD/%s/%s/%s/%s/%s/%s/%s/%s/%s/%s/%s", MACAddressWiFi, MACAddressWAN, Uptime, HW_VER, SoftwareVersion, SIOD_ID, \
						IPAddressWiFi, IPMaskWiFi, IPAddressWAN, IPMaskWAN, Gateway, DNS1, DNS2, DHCP);

				printf("Sent: %s\n", msg);

				broadcast(msg);

            }
            break;
		/*
		ConfigRes/MACAddressWiFi/MACAddressWAN/UpTime/UnitType/HardwareVersion/SoftwareVersion/AAAA/IPAddressWiFi/IPMaskWiFi/IPAddressWAN/IPMaskWAN/Gateway/DNS1/DNS2/DHCP
			MACAddressWiFi:				0a:ba:ff:10:20:30
			MACAddressWAN:				0a:cc:10:bb:ab:00
			Uptime: (Linux in seconds)	123.43		
			UnitType: 					SIOD, AsteriskPC, Intercom
			HardwareVersion: 			0.1
			SoftwareVersion: 			0.5(it is convenient to get this from /etc/banner)
			AAAA: (optional)			SIOD ID Only if the UnitType is SIOD
			IPAddressWiFi:(optional)	10.10.0.55
			IPMaskWiFi:(optional)		255.255.255.0
			IPAddressWAN:(optional)		20.20.0.10
			IPMaskWAN:(optional)		255.255.255.0
			Gateway:(optional)			10.10.0.1
			DNS1:(optional)				8.8.8.8
			DNS2:(optional)				4.4.4.4
			DHCP:(optional)				Static, dhcp

		Does noting at the moment
		*/
        case ConfigRes:{

				if(verbose) printf("Rcv: ConfigRes\n");

            }
            break;
		/*
		Config/MACAddress/IPAddress/IPMask/Gateway/DNS1/DNS2/DHCP
			MACAddress:			0a:ba:ff:10:20:30
			IPAddress:			10.10.0.55
			IPMask:				255.255.255.0
			Gateway:(optional)	10.10.0.1
			DNS1:(optional)		8.8.8.8
			DNS2:(optional)		4.4.4.4
			DHCP:				Static, dhcp

		We set our network parameters
		*/
        case Config:{

				char *MACAddress, *IPAddress, *IPMask, *Gateway, *DNS1, *DNS2, *DHCP;
				char MACAddressWAN[STR_MAX],  MACAddressWiFi[STR_MAX];
				unsigned long long MACAddress_num;

				if(n_args != 8) {
					printf("Wrong format of Config message\n");
					return -1;
				}
				MACAddress=args[1]; IPAddress=args[2]; IPMask=args[3]; Gateway=args[4]; DNS1=args[5]; DNS2=args[6]; DHCP=args[7];

				MACAddress_num = MACaddress_str2num(MACAddress);
				if(eth1MAC() == MACAddress_num){ /* We set WAN parameters*/

					uciset("network.wan.ipaddr", IPAddress);
					uciset("network.wan.netmask", IPMask);
					uciset("network.wan.gateway", Gateway);
					uciset("network.wan.dns", DNS1);
					//uciset("network.wan.dns", DNS2); ignore for now
					uciset("network.wan.proto", DHCP);

					ucicommit();

					if(verbose) printf("WAN Config commited\n");

				} else if(wifiMAC() == MACAddress_num){ /* We set WiFi parameters*/

                    uciset("network.mesh_0.ipaddr", IPAddress);
                    uciset("network.mesh_0.netmask", IPMask);
                    uciset("network.mesh_0.gateway", Gateway);
                    uciset("network.mesh_o.dns", DNS1);
                    //uciset("network.mesh_0.dns", DNS2); ignore for now
                    uciset("network.mesh_0.proto", DHCP);

					ucicommit();
			
					if(verbose) printf("WiFi Config commited\n");

				}

            }
            break;
		/*
		RestartNetworkService/MACAddress
			MACAddress:(optional)	0a:ba:ff:10:20:30

		We restart network services
		*/
        case RestartNetworkService:{
				char *MACAddress;
				unsigned long long MACAddress_num;

				MACAddress=args[1];				

				if(verbose) printf("Rcv: RestartNetworkService\n");
				
				if (*MACAddress == '\0'){	
					restartnet(); /* The optional MAC address is omitted so we restart our network service */
					
					if(verbose) printf("Restart the network service\n");
					break;
				}

				MACAddress_num = MACaddress_str2num(MACAddress);
				if(eth1MAC() == MACAddress_num || wifiMAC() == MACAddress_num) {
					restartnet();

					if(verbose) printf("Restart the network service\n");
				}				

            }
            break;
        case RestartAsterisk:{

				if(verbose) printf("Rcv: RestartAsterisk\n");

            }
            break;
        case ConfigAsterisk:{

				if(verbose) printf("Rcv: ConfigAsterisk\n");

            }
            break;
        case AsteriskStatReq:{

				if(verbose) printf("Rcv: AsteriskStatReq\n");

            }
            break;
        case AsteriskStatRes:{
			
				if(verbose) printf("Rcv: AsteriskStatRes\n");

            }
            break;
        case ConfigNTP:{
				char *NTPServer0, *NTPServer1, *NTPServer2, *NTPServer3, *enable_disable, *SyncTime;

                if(n_args != 7) {
                    printf("Wrong format of ConfigNTP message\n");
                    return -1;
                }				
				NTPServer0=args[1]; NTPServer1=args[2]; NTPServer2=args[3]; NTPServer3=args[4]; enable_disable=args[5]; SyncTime=args[6];

				ucidelete("system.ntp.server");
				ucicommit();

				uciadd_list("system.ntp.server", NTPServer0);
                if (*NTPServer1) uciadd_list("system.ntp.server", NTPServer1);                
				if (*NTPServer2) uciadd_list("system.ntp.server", NTPServer2);                
				if (*NTPServer3) uciadd_list("system.ntp.server", NTPServer3);
				uciset("system.ntp.enable_server", enable_disable);
				//SyncTime - ignore for now
				ucicommit();

				if(verbose) printf("NTP configurations updated\n");				

            }
            break;
        case Set:{

				int res;
				char *X, *Y;
			
				if(verbose) printf("Rcv: Set\n");
	
				X=args[1]; Y=args[2];

				//res = setgpio(X, Y);
				
				if(!res){
					sprintf(msg, "JNTCIT/Put/%s/%s/%s", SIOD_ID, X, Y);

					printf("Sent: %s\n", msg);

					broadcast(msg);
				}	

            }
            break;
        case SetIf:{

                if(verbose) printf("Rcv: SetIf\n");

            }
            break;
        case TimeRange:{

                if(verbose) printf("Rcv: TimeRange\n");

            }
            break;
        case TimeRangeOut:{

                if(verbose) printf("Rcv: TimeRangeOut\n");

            }
            break;        
		case Get:{

                int res;
                char *X, Y[STR_MAX];

                if(verbose) printf("Rcv: Get\n");

                X=args[1];

                //res = getgpio(X, Y);

                if(!res){
                    sprintf(msg, "JNTCIT/Put/%s/%s/%s", SIOD_ID, X, Y);

                    printf("Sent: %s\n", msg);

                    broadcast(msg);
                }
            }
            break;        
		case Put:{

                if(verbose) printf("Rcv: Put\n");

            }
            break;        
		case GSTCheckSumReq:{


                if(verbose) printf("Rcv: GSTCheckSumReq\n");

            }
           	break;        
		case GSTCheckSum:{

                if(verbose) printf("Rcv: GSTCheckSum\n");

            }
            break;        
		case GSTReq:{

                if(verbose) printf("Rcv: GSTReq\n");

            }
            break;
        case GSTdata:{

                if(verbose) printf("Rcv: GSTdata\n");

            }
            break;
        case Ping:{

                if(verbose) printf("Rcv: Ping\n");

			}
            break;
        case PingRes:{

                if(verbose) printf("Rcv: PingRes\n");

			}
            break;

		default:
			if(verbose) printf("Wrong command.\n");
			return -1;
	}

	return 0;

}

/*
 * Removes spaces in string
 */
void RemoveSpaces(char* source)
{
	char *i = source;
	char *j = source;
  
	while(*j != 0){
    		*i = *j++;
    		if(*i != ' ') i++;
  	}

  	*i = 0;
}

/*
 * The function searches for the posible match of s2 inside s1
 * Returns 0 if match found 
 */
int strfind(const char *s1, const char *s2){

	int i, len1, len2;

	len1=strlen(s1);
	len2=strlen(s2);
	for(i=0; i<len1; i++){

		if(s1[i]==s2[0]){
			if (!strncmp(&(s1[i]), s2, len2)){
				break;
			}
		}
	}

	return (i==len1)?-1:0;
}
/*
 * The function extracts the arguments from the UDP datagram. 
 * Standard separator '/' is assumed
 */
int extract_args(char *datagram, char *args[], int *n_args){

	int i, len;
	
	*n_args=1;
	args[*n_args-1]=datagram;
	len=strlen(datagram);	
	for(i=0;i<len; i++){
		if(datagram[i] == '/'){
			args[(*n_args)++]=&datagram[i+1];
			datagram[i]='\0';
		}
	}
	
	return 0;	
}

/*
 * Calculates index of the command string so we can use C switch   
 */
int hashit(char *cmd) {

	int i;
	for(i=0; i<sizeof(cmds)/sizeof(char *); i++){
		if(!strcmp(cmd, cmds[i])) return i; 
	}

	return -1;
}

/*
 * Convert MAC address in string format xx:xx:xx:xx:xx:xx into u64 value   
 */
unsigned long long MACaddress_str2num(char *MACaddress){
	
	unsigned char mac[6];
	int ret;	

	ret=sscanf(MACaddress, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[5], &mac[4], &mac[3], &mac[2], &mac[1], &mac[0]);
	if(ret == 6)
		return (unsigned long long)mac[0] | ((unsigned long long)mac[1]<<8) | ((unsigned long long)mac[2]<<16) | ((unsigned long long)mac[3]<<24) | ((unsigned long long)mac[4]<<32) | ((unsigned long long)mac[5]<<40);
	else {
		if(verbose) printf("Wrong MAC address format.\n");
		return 0;
	}
		
}

/*
 * Convert MAC address from a u64 value into string format xx:xx:xx:xx:xx:xx
 * the string should be allocated by the caller
 */
void MACaddress_num2str(unsigned long long MACaddress, char *MACaddress_str){


    sprintf(MACaddress_str, "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned char)(MACaddress>>40), (unsigned char)(MACaddress>>32), (unsigned char)(MACaddress>>24),
															 (unsigned char)(MACaddress>>16), (unsigned char)(MACaddress>>8), (unsigned char)(MACaddress));

}

/*
 * Convert IP address in string format d.d.d.d into u32 value   
 */
unsigned long IPaddress_str2num(char *IPaddress){

    unsigned int ip[4];
    int ret;

    ret=sscanf(IPaddress, "%u.%u.%u.%u", &ip[3], &ip[2], &ip[1], &ip[0]);
    if(ret == 4)
        return (unsigned long)(ip[0]&0xff) | ((unsigned long)(ip[1]&0xff)<<8) | ((unsigned long)(ip[2]&0xff)<<16) | ((unsigned long)(ip[3]&0xff)<<24);
    else {
        if(verbose) printf("Wrong IP address format.\n");
        return 0;
    }

}

/*
 * Convert IP address from a u32 value into string format d.d.d.d
 * the string should be allocated by the caller
 */
void IPaddress_num2str(unsigned long IPaddress, char *IPaddress_str){


    sprintf(IPaddress_str, "%d.%d.%d.%d", (unsigned char)(IPaddress>>24), (unsigned char)(IPaddress>>16), (unsigned char)(IPaddress>>8), (unsigned char)(IPaddress));

}

/*
 * Convert string to upper case
 */
char *strupr(char *s){ 
	unsigned c; 
    unsigned char *p = (unsigned char *)s; 
    while (c = *p) *p++ = toupper(c);

	return s; 
}


/*
 * Retreive local eth0 MAC address
 */ 
unsigned long long eth0MAC(void){
	int fd;
	char mac[18];
	
	fd = open("/sys/class/net/eth0/address", O_RDONLY);

	read(fd, mac, 18);

	close(fd);

	return MACaddress_str2num(mac);
}

/*
 * Retreive local eth1 MAC address
 */
unsigned long long eth1MAC(void){
    int fd;
    char mac[18];

    fd = open("/sys/class/net/eth1/address", O_RDONLY);

    read(fd, mac, 18);

    close(fd);

    return MACaddress_str2num(mac);
}

/*
 * Retreive local WiFi MAC address
 */
unsigned long long wifiMAC(void){
    int fd;
    char mac[18];

    fd = open("/sys/class/net/wlan0/address", O_RDONLY);

    read(fd, mac, 18);

    close(fd);

    return MACaddress_str2num(mac);
}

/*
 * Execute uci get command to retreive a value from the openwrt configuration files 
 * Value should have at least STR_MAX bytes alocated.
 * returns 0 on success
 */
int uciget(const char *param, char *value){

    FILE *fp;
	char *ret, str[100];
	int i, len;	



	sprintf(str, "uci get %s", param);

    fp=popen(str,"r");
    ret=fgets(value, STR_MAX, fp);
    pclose(fp);

	len=strlen(value);
	if(value[len-1]=='\r' || value[len-1]=='\n') value[len-1]='\0';

	if(ret==NULL){
		value[0]='\0';
		return -1;
	}else
		return 0;
}

/*
 * Execute uci set command to update value to the openwrt configuration files
 * Note that you have to commit the change afterwords
 * returns 0 on success
 */
int uciset(const char *param, const char *value){

    FILE *fp;
    char str[100];
	char dummy[STR_MAX];

    sprintf(str, "uci set %s=%s 2>&1", param, value);

    fp=popen(str,"r");
    fgets(dummy, STR_MAX, fp);
    pclose(fp);

    if(!strfind(dummy, "Invalid"))
        return -1;
    else
        return 0;
}


/*
 * Execute uci delete command in order to delete an option 
 * or all list items from  the openwrt configuration files
 * Note that you have to commit the change afterwords
 * returns 0 on success
 */
int ucidelete(const char *param){

    FILE *fp;
    char str[100];
    char dummy[STR_MAX];

    sprintf(str, "uci delete %s 2>&1", param);

    fp=popen(str,"r");
    fgets(dummy, STR_MAX, fp);
    pclose(fp);

    if(!strfind(dummy, "Invalid"))
        return -1;
    else
        return 0;
}


/*
 * Execute uci add_list command in order to add new item to the list 
 * Note that you have to commit the change afterwords
 * returns 0 on success
 */
int uciadd_list(const char *param, const char *value){

    FILE *fp;
    char str[100];
    char dummy[STR_MAX];

    sprintf(str, "uci add_list %s=%s 2>&1", param, value);

    fp=popen(str,"r");
    fgets(dummy, STR_MAX, fp);
    pclose(fp);

    if(!strfind(dummy, "Invalid"))
        return -1;
    else
        return 0;
}


/*
 * Commit all changes (in the config files) done by uci set commands
 */
void ucicommit(void){

    FILE *fp;

    fp=popen("uci commit","r");
    pclose(fp);
}


/*
 * Restart network services
 */
void restartnet(void){

    FILE *fp;

	char dummy[STR_MAX];

    fp=popen("/etc/init.d/network reload 2>&1","r");

	while(fgets(dummy, STR_MAX, fp) != NULL){
		printf("%s\n", dummy);

	}

    if(pclose(fp)==-1)
		printf("Issue reloading network services");

}



/*
 * Get the uptime in seconds.
 * Value should have at least STR_MAX bytes alocated.
 *
 */
void uptime(char *uptime){

    FILE *fp;
	int len;

    fp=popen("cut -d ' ' -f 1 </proc/uptime","r");

    fgets(uptime, STR_MAX, fp);

	len=strlen(uptime);
    if(uptime[len-1]=='\r' || uptime[len-1]=='\n') uptime[len-1]='\0';

}

/*
 * Get the software version.
 * ver should have at least STR_MAX bytes alocated.
 */
int getsoftwarever(char *ver){

    FILE *fp;
	char *ret;	
	int len;

    fp=popen("cat /etc/banner | grep 'Version: .*'| cut -f3- -d' '","r");
    ret=fgets(ver, STR_MAX, fp);
    pclose(fp);

	len=strlen(ver);
    if(ver[len-1]=='\r' || ver[len-1]=='\n') ver[len-1]='\0';

    if(ret==NULL)
        return -1;
    else
        return 0;
}


/*
 * Broadcast UDP message
 */
int broadcast(char *msg){
	
	return(sendto(bcast_sockfd,msg,strlen(msg),0, (struct sockaddr *)&bcast_servaddr,sizeof(bcast_servaddr)));

}


/*
 * Unicast UDP message
 */
int unicast(char *msg){

	//TBD
    //return(sendto(bcast_sockfd,msg,strlen(msg),0, (struct sockaddr *)&bcast_servaddr,sizeof(bcast_servaddr)));

}

/*
 * Initialize SIOD GPIOs. 
 * The function opens 'value' file descriptors
 */
int gpios_init(void){

	int fd, n, fb0, fb1, fb2, fb3;
	char value[2], str[STR_MAX];

	GPIOs=0;

	/* Export the GPIOS */
	fd = open("/sys/class/gpio/export", O_WRONLY);
	n = snprintf(str, STR_MAX, "%d", REL0);
	write(fd, str, n);
	n = snprintf(str, STR_MAX, "%d", REL1);
	write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", S_R);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", PULSE);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", FB0);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", FB1);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", FB2);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", FB3);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", IN0);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", IN1);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", IN2);
    write(fd, str, n);
    n = snprintf(str, STR_MAX, "%d", IN3);
    write(fd, str, n);
	close(fd);

	/* Configure inputs */
	snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", FB0);
	fd = open(str, O_WRONLY);
	write(fd, "in", 2);
	close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", FB1);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", FB2);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", FB3);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", IN0);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", IN1);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", IN2);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);    
	snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", IN3);
    fd = open(str, O_WRONLY);
    write(fd, "in", 2);
    close(fd);


	/* read the inputs */
	snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", IN0);
	fd_in0 = open(str, O_RDONLY);
	read(fd_in0, value, 2); value[2]='\0';
	GPIOs |= ((!atoi(value)) << 4);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", IN1);
    fd_in1 = open(str, O_RDONLY);
    read(fd_in1, value, 2); value[2]='\0';
    GPIOs |= ((!atoi(value)) << 5);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", IN2);
    fd_in2 = open(str, O_RDONLY);
    read(fd_in2, value, 2); value[2]='\0';
    GPIOs |= ((!atoi(value)) << 6);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", IN3);
    fd_in3 = open(str, O_RDONLY);
    read(fd_in3, value, 2); value[2]='\0';
    GPIOs |= ((!atoi(value)) << 7);

	
	/* read the relay feedbacks */
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", FB0);
    fd_fb0 = open(str, O_RDONLY);
    read(fd_fb0, value, 2); value[2]='\0';
    fb0=atoi(value);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", FB1);
    fd_fb1 = open(str, O_RDONLY);
    read(fd_fb1, value, 2); value[2]='\0';
    fb1=atoi(value);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", FB2);
    fd_fb2 = open(str, O_RDONLY);
    read(fd_fb2, value, 2); value[2]='\0';
    fb2=atoi(value);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", FB3);
    fd_fb3 = open(str, O_RDONLY);
    read(fd_fb3, value, 2); value[2]='\0';
    fb3=atoi(value);


	/* configure outputs */
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", REL0);
    fd = open(str, O_WRONLY);
    write(fd, "low", 3);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", REL1);
    fd = open(str, O_WRONLY);
    write(fd, "low", 3);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", S_R);
    fd = open(str, O_WRONLY);
    write(fd, "low", 3);
    close(fd);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/direction", PULSE);
    fd = open(str, O_WRONLY);
    write(fd, "high", 4);
    close(fd);

	/* Relays descriptors */
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", REL0);
    fd_rel0 = open(str, O_WRONLY);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", REL1);
    fd_rel1 = open(str, O_WRONLY);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", S_R);
    fd_s_r = open(str, O_WRONLY);
    snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", PULSE);
    fd_pulse = open(str, O_WRONLY);    


	/* Set the output information in GPIOS 
     * GPIOS = [IN3 IN2 IN1 IN0 OUT3 OUT2 OUT1 OUT0]
   	 *          MSB                            LSB   
	 */
	GPIOs |= !(fb0);
	GPIOs |= (!(fb1) << 1);
	GPIOs |= (!(fb2) << 2);
	GPIOs |= (!(fb3) << 3);

	/* Initialize the file descriptors in an array to ease indexing */
	IOs[0]=fd_fb0; IOs[1]=fd_fb1; IOs[2]=fd_fb2; IOs[3]=fd_fb3;
	IOs[4]=fd_in0; IOs[5]=fd_in1; IOs[6]=fd_in2; IOs[7]=fd_in3;

	if(verbose) printf("GPIOs = 0x%x\n", GPIOs);

	return 0;
}


/*
 * iet local gpio
 *
 *	X:(optional)	index of the output [0, 1, .. 3] Current version of SIOD supports 4 outputs. 
 *					X can be empty string. If empty it is assumed that YYYY specifies the state of all outputs. 
 *	Y:  			Active/not active "0" or "1"
 *	YYYY:			represent 4 digit binary number.(We have 4 outputs per SIOD) LSB specifies the state of the first IO, 			
 *					MSB of the 4th output. 
 *
 */


int setgpio(char *X, char *Y){

	int x, n, xlen, ylen;
	char str[STR_MAX];
	
	xlen=strlen(X);
	ylen=strlen(Y);

	if(xlen == 1 && ylen == 1){
		
		x=atoi(X);
		if (x < 0 || x > OUTPUTS_NUM) {
			if(verbose) printf("Output index out of range, ignoring\n");
			return -1;
		} else if (Y[0] !='0' && Y[0] !='1') {
			if(verbose) printf("Output value should be 0 or 1\n");
			return -1;
		}
    
    	write(fd_rel0, (x>1)?"1":"0", 1); write(fd_rel1, (x%2)?"1":"0", 1);
    	write(fd_s_r, Y, 1);
    	write(fd_pulse, "0", 1); usleep(200000L); write(fd_pulse, "1", 1);
		      
		GPIOs = (Y[0]-'0')?(GPIOs|(1<<x)):(GPIOs&~(1<<x));

		if(verbose == 2) printf("Set: OUT%d = %s\n", x, Y);

	} else if (xlen == 0 && ylen == OUTPUTS_NUM){
		int i;
		
		for(i=0;i<ylen;i++){
			if (Y[i] == '0' || Y[i] == '1'){
				
					
        		write(fd_rel0, (i>1)?"1":"0", 1); write(fd_rel1, (i%2)?"1":"0", 1);
        		write(fd_s_r, (Y[OUTPUTS_NUM-1-i]-'0')?"1":"0", 1);
        		write(fd_pulse, "0", 1); usleep(200000L); write(fd_pulse, "1", 1);

				GPIOs = (Y[OUTPUTS_NUM-1-i]-'0')?(GPIOs|(1<<i)):(GPIOs&~(1<<i));

				if(verbose == 2) printf("Set: OUT%d = %d\n", i, Y[OUTPUTS_NUM-1-i]-'0');
			}
		}

	} else {
		printf("setgpio: Invalid X and Y\n");
		return -1;
	} 

	return 0;

}

/*
 * Get local gpio
 * 
 *   X:(optional)    index of the output [0, 1, .. 8], 
 *					optional argument, if empty the state of all IOs are returned
 *                   results are returned in a strin Y. It must be alocated by the caller 
 */


int getgpio(char *X, char *Y){

    int x, xlen;
    char str[STR_MAX];

    xlen=strlen(X);
    if(xlen == 1){

        x=atoi(X);
        if (x < 0 || x > 8) {
            if(verbose) printf("IO index out of range, ignoring\n");
            return -1;
		}

        snprintf(str, STR_MAX, "/sys/class/gpio/gpio%d/value", IOs[x]);
        read(IOs[x], Y, 2); Y[2]='\0';
		Y[0]=(Y[0]=='0')?'1':'0'; //Invererse logic for the inputs and feedbacks

		if(verbose==2) printf("Get: IO%d = %s\n", x, Y);

    } else if (xlen == 0){
        int i;
        for(i=0;i<INPUTS_NUM+OUTPUTS_NUM;i++){
			read(IOs[i], str, 2); str[2]='\0';
			Y[INPUTS_NUM+OUTPUTS_NUM-1-i]=(str[0]=='0')?'1':'0'; //Invererse logic for the inputs and feedbacks

			if(verbose==2) printf("Get: IO%d = %s\n", i, (str[0]=='0')?"1":"0");
        }
		Y[i]='\0';

    } else {
        printf("getgpio: X must be empty or represent a number \n");
        return -1;
    }

    return 0;
}

