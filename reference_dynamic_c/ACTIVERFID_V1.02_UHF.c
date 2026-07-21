// 		Copyright RFID Timing Pty Ltd
// 		U1, 97 Garling St, O'Connor
// 		Western Australia
// 		www.rfidtiming.com
// 		ActiveRFID.C for rabbitcore 6700 series
// 		Author: Andrew Peterson
//			Start Date: August 2020


// This macro causes the FAT library to wait for everything to complete
// before returning to the caller.  This makes the application MUCH simpler.
#memmap xmem
#define FAT_BLOCK

// Uncomment to turn on Debug options
//#define FAT_DEBUG
//#define NFLASH_DEBUG	// only useful for boards with nand flash
//#define SFLASH_DEBUG	// only useful for boards with serial flash
//#define FATFTC_DEBUG
//#define PART_DEBUG
//#define ECC_DEBUG
//efine SPI_DEBUG    //remove this after full testing


/////////////////////////////////////////////

// This version uses the RCM6760 because of the larger flash size useful for higher numbers of UHF tag reads
// It is a different version to the Active only using RCM6710


// FS2 file system definitions
#define	RAM_BUFFER_FILE_NAME		2
#use "GENIE2.LIB"
#use "I2C_DS3231.lib"
#use "NEOM8T.lib"
//#use "Echo_Firmware_Download.lib"
#use "idblock_api.lib"
#use "EchoFilesystem.lib"
#use "4G_Modem.lib"
#use "UHF_READER.LIB"
#use "FinishLynx.lib"

#define SPI_SER_D    //serial port B used

#define CLOCK_PORT E
#define CLOCK_BIT 3
#define SPI_CLK_DIVISOR 25              //around 1MBit
//#define SPI_CLK_DIVISOR 100
#define CS_PORT			PBDR
#define CS_PORTSHADOW	PBDRShadow
#define CS1_BIT			7        //NRF82833
#define CS2_BIT			5        //GPS Chip

#define CS1_ENABLE	 BitWrPortI ( CS_PORT, &CS_PORTSHADOW, 0, CS1_BIT )
#define CS1_DISABLE BitWrPortI ( CS_PORT, &CS_PORTSHADOW, 1, CS1_BIT )
#define CS2_ENABLE	 BitWrPortI ( CS_PORT, &CS_PORTSHADOW, 0, CS2_BIT )
#define CS2_DISABLE BitWrPortI ( CS_PORT, &CS_PORTSHADOW, 1, CS2_BIT )
#define SPIMODE	2
#define REC_LENGTH	20
#define TIME_DIFF_MS 	2			//millisconds that DS3231 and ublox do not agree on 1 HZ signal
#define DELAY 10000
#define BEEPDELAY 300  //time the buzzer is left on after chip(s) read
#define DIMDELAY  20    //20 seconds till touchscreen dims from non use
// Beeper states
#define MAX_RECORDS 40

#define MAX_TCP_SOCKET_BUFFERS 10
#define MAX_SOCKET_CONNECTIONS 3 // Number of allowed client socket connections
#define UDP_SOCKETS 6   // allow enough for downloader and DHCP
#define MAX_UDP_SOCKET_BUFFERS 6
#define KEEPALIVE_WAITTIME	15		// 75 second wait
#define KEEPALIVE_NUMRETRYS	1	// retry 8 times
#define DATA_PORT 23
#define RESET_PORT 8001 // For resetting the data socket
// Rewind while reading states
#define RWR_STOPPED 0
#define RWR_STARTING 1
#define RWR_READING 2

#define TRIGGER_DELAY 3000 //ms till next trigger input allowed
#define IDLE 0 //not reading in UHF mode
#define READING 2 // Read operation in process, main menu not there

//#define TCPCONFIG    7    //configured in Echo_Firmware_Download.lib
#define TCPCONFIG    5
#define USE_DHCP

// Output types
#define OUTPUT_DEC 0
#define OUTPUT_HEX 1


/////////////////////////////////////////////////

// Configuration Options

/* Set FIRMWARE_URL to a URL of a .bin file compiled with Dynamic C
 * For this sample, we're using multiple board-specific binaries stored on a
 * Digi web server.
 */

/*** BeginHeader */
// The board version needs to be chosen on first blow of firmware. If a 6750 is used then need
// to choose the 6750 board when compiling. Firmware updates need to be saved in both versions and updated on website
//#define FIRMWARE_URL \
 //	("http://www.rfidtiming.com/Software/ACTIVERFID_6760.bin")
#define FIRMWARE_URL \
 	  ("http://www.rfidtiming.com/Software/ACTIVERFID_6750.bin")
//     #define FIRMWARE_URL \
// 	  ("http://www.rfidtiming.com/Software/ACTIVERFID_6710Plus.bin")

#define CHECK_URL \
 	("http://www.rfidtiming.com/Echo_VPlus.htm")
//MAX_FIRMWARE_BINSIZE=0x60000            //only for 6710 model, comment out for 6760
//_FIRMWARE_NAME_ = "ACTIVERFID_6710Plus"   //add for 6710 models


/*
 * Unless BU_ENABLE_SECONDARY was defined in the Global Macro Definitions,
 * define one of the following macros to select the temp storage location.
 * On the RCM5600W, the only option is to write new firmware directly to the
 * boot flash.  This is dangerous, as a power failure during the download will
 * result in a board that needs to be reloaded with the RFU or some other
 * direct serial connection on the programming port.
 */
//	#define BU_TEMP_USE_FAT				// use file on FAT filesystem
//	#define BU_TEMP_USE_SBF				// use unused portion of serial boot flash
//	#define BU_TEMP_USE_SFLASH			// use serial data flash (without FAT)
//	#define BU_TEMP_USE_DIRECT_WRITE	// write directly to boot firmware image

/*
 * If using the serial data flash as a target (BU_TEMP_USE_SFLASH), you can
 * specify a page offset (other than the default of 0) for storing the
 * temporary firmware image.
 */
//	#define BU_TEMP_PAGE_OFFSET 0



// End of Configuration Options

// Make sure an option has been enabled.
#if ! defined BU_TEMP_USE_FAT && \
	 ! defined BU_TEMP_USE_SBF && \
	 ! defined BU_TEMP_USE_SFLASH && \
	 ! defined BU_TEMP_USE_DIRECT_WRITE && \
	 ! defined BU_ENABLE_SECONDARY
#fatal "You must uncomment a BU_TEMP_USE_xxx macro at the top of this sample."
#endif


#define DHCP_CLASS_ID "Rabbit6760-TCPIP:Rabbit:ECHOACTIVE"
// This macro causes the MAC address to be used as a unique client
// identifier.
#ifndef DHCP_CLIENT_ID_MAC
	#define DHCP_CLIENT_ID_MAC
#endif

// default functions to xmem
//#memmap xmem

#ifdef BU_TEMP_USE_FAT
	// Set FAT library to blocking mode (optional, required if using uC/OS-II)
	// #define FAT_BLOCK

	// Set file system to use forward slash as directory separator (optional)
	#define FAT_USE_FORWARDSLASH

	// Load the FAT filesystem library
	#use "fat16.lib"
#endif





// Shutdown control
#define NORMAL_SHUTDOWN 0
#define ABNORMAL_SHUTDOWN 1







///////////////////////////////////////////////////
#memmap xmem
#use "tcp_config.lib"	// This is not normally required, but we bring it in here
								// so that following macro settings may override those in the config.7

#use dcrtcp.lib   //already declared in firmware download lib
#use SPI.LIB

#use "http_client.lib"
#use "board_update.lib"



int	BT_ON_TIMES[8] = {0, 1, 2, 3, 5, 8, 12, 24} ;

int 						batt_percent; // Percent of li-ion battery
int 						screenSleep; // = 1 when LCD is in low contrast for power saving
long						checkInterval;  //timer for battery display
long						checkInterval2;  //timer for sats display
int						time_diff_pps;  //difference in milliseconds between DS3231 and Ublox
char 						cmdi[7]; //commands to the nrf52833 via SPI
char 						gating;
char						pps, old_pps;  //pps signal from ublox
char 						ds_rollover;    //1HZ signal from DS3231, = 1 on rollover
unsigned long			nrftime;  // seconds since 1/1/2020 for conversing with nrf52833
char						set_time; // 0 if no GPS timeset, 1 if PPS timeset
unsigned int			chip_reads;
unsigned long			iLastLogID;     //the last logID recorded in the log file
unsigned long	 		LastBeepTime;
unsigned long			LastTouchTime;     //last touch of 4D display for auto dimming
void* 					SettingsArray[1];  // Used for saving settings
unsigned int 			SettingsLenArray[1];  // Used for saving settings
unsigned long     	RWRFromTime, RWRToTime; // The date/time range for rewind while reading
char						RWRType; // 6 or 8. 6 = rewind using record number, 8 = rewind using date/time
int						RWRState; // Rewind while reading state
unsigned long			iRewindFromTime, iRewindToTime; // Keeps a record of from and to time for rewind while reading
char						iOnlyRewindUnsentData; // Used with Settings.DataOnRequest. If this is 1, then the rewind while read only sends unsent records
char						iRewindSocket; // The socket index which requested the rewind. Rewind data is only sent to that socket.
char						cRewindPending;	// See iSendDataToPort
char						sRewindPendingBuf[100]; // See cRewindPending
char						iCurrentConnectionCount; // Number of current socket connections from clients. Used to update screen status
char					iSendDataToPort;	// If 1, then live data is sent to socket. If 0, then no data sent (see Settings.DataOnRequest)
												// If this is 0 and a rewind is done, then the data is sent when this goes on again.
unsigned long		iLastStatusTime; // The status messages are only sent to the PC every 10 seconds.
long 						dynIP;          //added dynamic IP address given
long 						dynNetMask;      //added netmask from DHCP
char					MacAddress[6];
unsigned int					version; //the firmware version 0x0000
unsigned long 		iDSTimeFromRabbit;   //the rabbit MS_TIMER value on 1HZ DS3231 interrupt
int 					new_firmware_avail;      //is 1 if we check and there is a new version
int 					new_fw_vers;                 //the new version avail
char 					spi_record_state;  //1 for records avail, 2 for the records txfr
char 					record_type;       //global as changes depending on live or rewind nrf records
char 					ret_records; 	//number of records to be retrieved from NRF52833
char					encode_tag; //stop all operations while waiting for user to input code
char					transmitter_off; //turns off LF loop
int					nrf52833_FW;   //the FW version installed on the base chip
char 					admin;  //0 = normal, 1 = admin rights to change txpdr codes and do dfu
char					enter_pin;    //state for setting pin for admin rights
char 					set_time_nrf;
unsigned int      board_vers;
long 		next_trigger;
char		trigger_time;
unsigned long FromTime, ToTime;   //now global as all rewind is done 1 record at time during state machine
char RewindType;
char doRewind;
char lo_backlight;       //added to reduce load on system in UHF reading mode
char diag_visible;
char trigger_dim;

// Saved (NAND) settings of system
typedef struct TSettings
{
      int Init;
      char RabbitIP[4]; // IP address of rabbit
      char Beeper; // 0 = off, 1 = on
      char ReaderPower; // Values 0 - 100 %
      int TimeZone;
      int Add30;
      char AutoSetGPSTime;
      char Channel; // ID of box starts ar 0
		char RemoteType; // //  0 = off, 1 = GPRS modem, 2 = LAN
      char GPRSServerIP1[4];
      word GPRSServerPort;
      char APNName[30];
      char APNUser[30];
      char APNPassword[30];
      unsigned long NANDCurrentRec, RAMCurrentRec; // Record numbers of last chip time sent to socket or GPRS
      char DataOnRequest; // If true, then realtime data is only sent to the tcpip socket when requested by the user
      unsigned long iLastTimeSent; // The last time sent to the socket.
      char TriggerOn;
      unsigned long GPRS_CurrentRec; // Record number of last record sent (and acknowledged) via remote connection
      char RabbitGateway[4]; // IP address like: 192.168.1.1
      char RabbitDNS[4]; // IP address like: 192.168.1.1
      char SendDataToRemoteServer; // If 0, then we can still have a remote connection, but no data is sent, just config data to and fro
      char GPRSServerIP2[4];
      char GPRSServerIP3[4];
      int FirstBoot;
      char useDHCP;
      char Brightness;
      char Dim;
      char UHF_region;     // 0=FCC, 1=ETSI, 2=Aus
      char System; //Active or UHF
      int ShutDownStatus; // Set to NORMAL_SHUTDOWN when readers are stopped by the user. If the Ultra starts up and this is
      char OutputType; //hex or dec
};

char DynamicIP[4]; //new added to EchoActive - is the IP address deom DHCP

struct TSettings 	Settings;
char					GPSCoords[35]; // Looks something like this: 3203.416300,S,11548.131304,E
long 					dynIP;          //added dynamic IP address given
long 					dynNetMask;      //added netmask from DHCP

/*rabbit command structure
//	set a setting on nrf 0x01, 0xXX, 0xXX where XX are settings
//power    20 to 100%  in 10% increments
//channel       1 to 8                       bit 1-3  of LF dat transmission
//mode          0 = standard, 1 = encoding   bit 0   of LF dat transmission
//bt_on			7 settings           byte 2 bit 0-2

//  ie   0x01, 0x28, 0x60 for 60% power, channel 6 and mode 0
//able to add more bit settings in future
*/
typedef struct nrf_settings_cmd
{
   char bt_adv;    // selection of times
   char playback;
   char chip_program;
   char chip_sleep;
   char chip_code[6];
};

struct nrf_settings_cmd RFID;

/*
//reply
//0x01, 0xXX = confirm setting change where XX is response that txpdr data is avail buffer
//0x02, 8 bytes of txpdr data
// byte 1-8 = code   (64 bits)
// byte 9-10 = time_ticks (say ms from second change so 1/1000)
// byte 11 = battery state volts
// byte 12 = spare
*/
typedef struct nrf_record
{

    unsigned long  date_time;
    unsigned int  ms;
    char   max_RSSI;
    char   wake_count;
    char  battery;
    unsigned int loop_data;
    char  xpdr_code[6];
    char HasBeenSent;
    unsigned long LogID;
};


char code[11] = {0,0,0,0,0,0,0,0,0,0,0};           //txpdr code global so can be used to populate display

tcp_Socket download_sock;       //socket to connect to http server for firmware download
tcp_Socket LANClientSocket;  //socket for Outreach comms

typedef struct TMyServerSocket
{
	tcp_Socket ServerSocket; // Connection to PC
   int ClientIsConnected;
};

tcp_Socket check_sock;       //HTTP socket for FW checks
httpc_Socket hsock;
//udp_Socket udp_broad_sock;      //UDP socket for sniffing available units on the LAN

void* 				SettingsArray[1];  // Used for saving settings
struct 	 			TMyServerSocket MyServerSocket[MAX_SOCKET_CONNECTIONS];
struct 	 			TMyServerSocket ResetSocket;
struct            TMyServerSocket UDPSocket;
// Riley - Struct for Finish Lynx Socket
struct 	 			TMyServerSocket FinishLynxSocket;

struct tm CurTime;
RTC_Time *mytime;
int time_offset; //will adjust the time when timezone changed

char  keyboard_string[30] = {'\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0'};
char keypad_string[15] = {'\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0'};
char txpdr_string[7] = {'\0','\0','\0','\0','\0','\0','\0'};

FATfile				RWR; // Handle for rewinding while reading

//------------------------------------------------------------------------
// Milli-sec delay function
//------------------------------------------------------------------------
nodebug void msDelay(unsigned int delay)
{
	auto unsigned long done_time;

	done_time = MS_TIMER + delay;
   while( (long) (MS_TIMER - done_time) < 0 );
}


void myDebugHandler(char *str){
	printf("%s\r\n", str);
}


void SaveSettings()
{
	writeUserBlockArray(0, SettingsArray, SettingsLenArray, 1);
   LastTouchTime = SEC_TIMER;
   //SyncMSTime(); // This MUST stay here. writeUserBlockArray affects MS_TIMER
   //set DS3231
    //strangely writing to the user block stuffs up the i2c to DS3231
    //have to reinitialise it on each write to user block
   i2c_init();
}

void Beep(void)
{
   LastBeepTime = MS_TIMER;
   BitWrPortI(PBDR, &PBDRShadow, 1, 2);
}






//have to update the strings from settings as keypad/keyboard exit does not create an event in form activation
void updateNetworkStrings(void)
{
	char char_string[20];


   if(Settings.useDHCP) sprintf(char_string, "%d.%d.%d.%d", DynamicIP[0], DynamicIP[1], DynamicIP[2], DynamicIP[3]);
   else sprintf(char_string, "%d.%d.%d.%d", Settings.RabbitIP[0], Settings.RabbitIP[1], Settings.RabbitIP[2], Settings.RabbitIP[3]);
   genieWriteStr(GENIE_LAN_STR, char_string);
   sprintf(char_string, "%d.%d.%d.%d", Settings.GPRSServerIP1[0], Settings.GPRSServerIP1[1], Settings.GPRSServerIP1[2], Settings.GPRSServerIP1[3]);
   genieWriteStr(GENIE_REMOTE_STR, char_string);
   sprintf(char_string, "%d.%d.%d.%d", Settings.RabbitGateway[0], Settings.RabbitGateway[1], Settings.RabbitGateway[2], Settings.RabbitGateway[3]);
   genieWriteStr(GENIE_GATEWAYIP_STR, char_string);
   sprintf(char_string, "%d", Settings.GPRSServerPort);
   genieWriteStr(GENIE_PORT_STR, char_string);
   genieWriteStr(GENIE_APN_STR, Settings.APNName);
   sprintf(char_string, "%02X:%02X:%02X", MacAddress[3], MacAddress[4], MacAddress[5]);
   genieWriteStr(GENIE_MAC_STR, char_string);
   genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_DHCP, Settings.useDHCP);
   if(Settings.RemoteType){
   	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_REMOTE, 1);
   }else{
   	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_REMOTE, 0);
   }


}

//A callback that is triggered if dynamic IP is assigned or timeout

void updateDIPA()
{
	auto DHCPInfo * di;
   auto word dhcp_ok, dhcp_fb;
   auto long myip;
   auto long mynetmask;
   char *ptr1, *ptr2;
   int	i;

   printf("Updating ethernet\n");

	if (ifconfig(IF_ETH0,
         IFG_DHCP_INFO, &di,
         IFG_DHCP_OK, &dhcp_ok,
         IFG_DHCP_FELLBACK, &dhcp_fb,
         IFG_IPADDR, &dynIP,
         IFG_NETMASK, &dynNetMask,
         IFS_END)
      || !di) {
      printf("No DHCP info obtained!\n");
	}
   if(di){
   	if (dhcp_ok){
         ptr1 = &dynIP;
         ptr2 = &di->dhcp_server;
         for(i=3;i>-1;i--){
         	DynamicIP[i] = *ptr1;                //fill out the dynamic IP and router IP
            Settings.RabbitGateway[i] = *ptr2;
            ptr1++;
            ptr2++;
         }
         Settings.useDHCP = 1;
   	}
   }

   if (dhcp_fb) {
      printf("DHCP fell back to defaults\n");
      Settings.useDHCP = 0; //turn off DHCP
   }
   if(ifpending(IF_ETH0) == IF_UP){
      updateNetworkStrings();
   }
   ip_print_ifs();
	router_printall();
   arpcache_printall();

}

void UpdateRabbitIP()
{
   char sIP[20], sGateway[20], sDNS[20], i;
   auto word dhcp_ok, dhcp_fb;
   auto DHCPInfo * di;
   auto long myip;
   auto long mynetmask;
   //these will be fallbacks
   sprintf(sIP, "%d.%d.%d.%d", Settings.RabbitIP[0], Settings.RabbitIP[1], Settings.RabbitIP[2], Settings.RabbitIP[3]);
   sprintf(sGateway, "%d.%d.%d.%d", Settings.RabbitGateway[0], Settings.RabbitGateway[1], Settings.RabbitGateway[2], Settings.RabbitGateway[3]);
   sprintf(sDNS, "%d.%d.%d.%d", Settings.RabbitDNS[0], Settings.RabbitDNS[1], Settings.RabbitDNS[2], Settings.RabbitDNS[3]);
   ifconfig(IF_ETH0, IFG_DHCP_INFO, &di, IFS_END);	// Get whether interface is qualified for DHCP
	printf("Interface %d is %squalified for DHCP.\n", IF_ETH0, di ? "" : "NOT ");

   if(Settings.useDHCP){
      //DHCP on show attempt to get dynamic IP
		ifconfig(IF_ETH0,
	      IFS_ICMP_CONFIG, 1,     // Also allow use of directed ping to configure (only if DHCP times out).
         IFS_DHCP, di != NULL,	// Use DHCP if interface is qualified for it
         IFS_DHCP_FALLBACK, 1,
         IFS_IF_CALLBACK, updateDIPA,     //a callback to update the dynamic IP address setting
         IFS_DHCP_TIMEOUT, 8,      // Specify timeout in seconds
         IFS_DHCP_FB_IPADDR, aton(sIP),
         IFS_DHCP_QUERY, IF_P2P(IF_ETH0),
         IFS_UP,
         IFS_END);
      if (di)
      	// Specify fallbacks for DHCP...
	      ifconfig(IF_ETH0,
	         IFS_IPADDR, aton(sIP), // Create static IP
	         IFS_NETMASK, 0xFFFF0000uL,
	         IFS_END);
  }else{
       ifconfig(IF_ETH0,
       		IFS_DOWN,
            IFS_IPADDR, aton(sIP),
            IFS_ROUTER_SET, aton(sGateway),
            IFS_NAMESERVER_SET, aton(sDNS),
            IFS_DHCP, 0,        //turn off DHCP
            //IFS_IF_CALLBACK, updateDIPA,     //still want a callback
            IFS_NETMASK, 0xFFFF0000uL, // aton("255.255.0.0")
            IFS_UP,
            IFS_END);
  		printf("Static IP %s set\n", sIP);
  }

}


void SetRabbitIP()
{

   UpdateRabbitIP();

   printf("Starting network (DHCP timeout %d seconds)...\n", 8);
	printf("Done sock_init()\n");
   sethostname("RFIDTIMING EchoActive");
   //ifconfig(IF_ETH0, IFS_UP,
   //         IFS_END);

}





// This function creates the string to be sent to the PC socket
// It also returns the number of chars in the string
int CreateSockString(struct nrf_record logEntry, char IsRewind, char *s, unsigned long code, int uhf_code)
{
	char sChip[20];
   char loop_id;

   if(trigger_time){
      return   sprintf(s, "%lu,%d,%lu,%03d,%d,%d,%d,%d,%d,%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
	            (unsigned long)0,
	            0,
	            logEntry.date_time,
	            logEntry.ms,
	            Settings.Channel,
	            0,
	            0, // Is rewind
	            0,
	            0,
	            0, 0, 0, 0, 0, 0, 0, 0,
	            0,
	            logEntry.LogID
	            );
   }else{
      if(uhf_code){     //UHF data
         if (Settings.OutputType == OUTPUT_DEC)
			{
				sprintf(sChip, "%lu", code);
			}
			else if (Settings.OutputType == OUTPUT_HEX)
			{
				sprintf(sChip, "%lX", code);
			}
   		return   sprintf(s, "%lu,%s,%lu,%03d,%d,%d,%d,%d,%d,%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
	            (unsigned long)0,
	            sChip,
	            logEntry.date_time,
	            logEntry.ms,
	            logEntry.loop_data,
	            logEntry.max_RSSI,
	            IsRewind, // Is rewind
	            1,
	            Settings.Channel+1,
	            0, 0, 0, 0, 0, 0, 0, 0,
	            0,
	            logEntry.LogID
	            );
      }else{
      	sprintf(sChip, "%.6s", logEntry.xpdr_code);
   		loop_id = ((logEntry.loop_data >> 6) & 0x0F) + 1;
         if(logEntry.ms < 100){
         	printf("here");
         }
   		return   sprintf(s, "%lu,%s,%lu,%03d,%d,%d,%d,%d,%d,%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
	            (unsigned long)0,
	            sChip,
	            logEntry.date_time,
	            logEntry.ms,
	            loop_id,
	            logEntry.max_RSSI,
	            IsRewind, // Is rewind
	            logEntry.wake_count,
	            logEntry.battery,
	            0, 0, 0, 0, 0, 0, 0, 0,
	            0,
	            logEntry.LogID
	            );
      }
   }
}

/*
This procedure also saves the start time to RAM as a separate record as the start time
is not saved with the chip.
*/

// -------------------------------------------------------
// Writes a chip, it's RSSI and it's time to the PC socket
// A failed write loses the record
// This function is only used for live data, not rewinds.
// If WriteStartTime = 1 then write the MTB start time, not the chip time
// -------------------------------------------------------
void WriteChipToSocket(struct nrf_record logEntry, unsigned long LastLogID, unsigned long UHF_Code, int is_uhf)
{
	char buf[100];
   int i, j;


	   i = CreateSockString(logEntry, 0, buf, UHF_Code, is_uhf);
      /*
      if(USE_SER_FOR_CHIP_OUTPUT){      //added for client Mar 2021 - AP
      	serCopen(115200);
         serCwrFlush;
         serCwrite(buf, strlen(buf));
         serCclose;
      }
      */
      logEntry.LogID = LastLogID;
	   for (j = 0; j < MAX_SOCKET_CONNECTIONS; j++)
   	{
         if (MyServerSocket[j].ClientIsConnected)
         {
	   		i = sock_fastwrite(&MyServerSocket[j].ServerSocket, buf, i);
	         tcp_tick(NULL);
         }
      }

      Settings.iLastTimeSent = logEntry.date_time;
      // Riley - Create and Send Finish Lynx Data
      if(FINISH_LYNX_ACTIVE == 1)
      {
      	if (FinishLynxSocket.ClientIsConnected)
	      {
            i = CreateSockString_FinishLynx(logEntry, 0, buf);
	         i = sock_fastwrite(&FinishLynxSocket.ServerSocket, buf, i);
	         tcp_tick(NULL);
	      }
      }
      ///////////
}







/*
	This does a binary search of the log file and leaves the current
   file position where the StartTime was found.
   If a zero is returned, then there is nothing to rewind (StartTime too high)
   If the StartTime is zero, then we do a full rewind from the start of the file
*/
char BinarySearch(FATfile *NANDLogFile, unsigned long StartTime, char RewindType, unsigned long *iRecordNo)
{
   unsigned long iFirst, iLast, iPos, iFileSize;
   //long i;
   char bFound;
   int rc;
   struct nrf_record logEntry;

   if (StartTime == 0)
   {
	   fat_Seek(NANDLogFile, 0, SEEK_SET);
      *iRecordNo = 0;
   	return 1;
   }
   else
   {
   	// We first get the record numbers for the first and last records in the file.
      // iFirst will always be zero of course. If there are 100 records, then iLast = 100.

	   iFirst = 0;
	   fat_FileSize(NANDLogFile, &iFileSize);
	   fat_Seek(NANDLogFile, 0, SEEK_END);
	   rc = fat_Seek(NANDLogFile, -1.0 * sizeof(logEntry), SEEK_CUR);

	   rc = fat_Read(NANDLogFile, &logEntry, sizeof(logEntry));
	   iLast = iFileSize / sizeof(logEntry);
	   bFound = 0;

      // If the last records time is earlier than the start time, then just exit - no records to rewind
	   if ((RewindType == 8 && logEntry.date_time < StartTime)
         ||
         (RewindType == 6 && logEntry.LogID < StartTime))
	   {
	      return 0;
	   }

	   while (iFirst <= iLast && !bFound)
	   {

	      iPos = (iFirst + iLast) / 2; // Get the midpoint
         if (iFirst == 0 && iLast == 0) //(iPos <= 0)
         {
         	break;
         }
         //i = ((long)iPos * (long)sizeof(logEntry));
	      //rc = fat_Seek(NANDLogFile, (iPos * sizeof(logEntry)) + 1, SEEK_SET);
	      rc = fat_Seek(NANDLogFile, (iPos * sizeof(logEntry)), SEEK_SET);
	      rc = fat_Read(NANDLogFile, &logEntry, sizeof(logEntry));

	      if ((RewindType == 8 && logEntry.date_time == StartTime)
	         ||
	         (RewindType == 6 && logEntry.LogID == StartTime))
	      //if (logEntry.Seconds == StartTime)
	      {
	         bFound = 1;
	      }
	      else if ((RewindType == 8 && logEntry.date_time > StartTime)
	         		||
	        		  (RewindType == 6 && logEntry.LogID > StartTime))
	      //else if (logEntry.Seconds > StartTime)
	      {
         	if (iPos > 0) // Can't let iLast get less than zero
		         iLast = iPos - 1;
            else
            	iLast = 0;
	      }
	      else
	      {
	         iFirst = iPos + 1;
	      }
		}
      *iRecordNo = iPos;

      // we need to seek again here. The last fat_read after the fat_seek above
      // pushed the cursor past the record we want to start the rewind at.
		rc = fat_Seek(NANDLogFile, (iPos * sizeof(logEntry)), SEEK_SET);
		return 1;
   }
}



void Remote_GetRecordNoByDate()
// Used only by remote server for rewind
{
   int i;
   long prealloc;
   unsigned long Temp1, Temp2;
   struct nrf_record logEntry;


   //ProcessChipArray(&Temp1, &Temp2, 1);
   //if (DEBUG != 0) printf("Rewind from %lu to %lu\n", RWRFromTime, RWRToTime);

   prealloc = 0;
   fat_Open(
                 LogFilePartition,  // First partition pointer from fat_AutoMount()
                 LOG_FILENAME,   // Name of file.  Always an absolute path name.
                 FAT_FILE,    // Type of object, i.e. a file.
                 FAT_CREATE,  // Create the file if it does not exist.
                 &RWR,     // Fill in this structure with file details
                 &prealloc    // Number of bytes to allocate.
                );
   // Find the start location using a binary search
   if (BinarySearch(&RWR, RWRFromTime, 8, &Temp1))
   {
     Settings.GPRS_CurrentRec = Temp1;
     //if (DEBUG != 0) printf("Settings.GPRS_CurrentRec set to %lu\n", Settings.GPRS_CurrentRec);
   }
   fat_Close(&RWR);
}

cofunc RewindLogFile_BinarySearch(char RewindType)
{
	int i,b;
   char buf[100];
   unsigned long iRecords, iSent, code;
   struct nrf_record log_record;
   int is_uhf_code;
   char sChip[20];
   long prealloc;

   if(RWRState==RWR_READING)
   {
      //OpenNANDLogFile(0);
      prealloc = 0;
		fat_Open(LogFilePartition,  // First partition pointer from fat_AutoMount()
								LOG_FILENAME,   // Name of file.  Always an absolute path name.
								FAT_FILE,    // Type of object, i.e. a file.
								FAT_CREATE,  // Create the file if it does not exist.
								&RWR,     // Fill in this structure with file details
								&prealloc    // Number of bytes to allocate.
		);
   	if (BinarySearch(&RWR, FromTime, RewindType, &iSent))
   	{
      	iRecords = 0;
      	iSent = 0;
      	iRewindFromTime = FromTime;
      	iRewindToTime = ToTime;
      	do
      	{
      		i = fat_Read(&RWR, &log_record, sizeof(struct nrf_record));

         	if (i > 0)
         	{
         		if (FromTime == 0
            	|| (RewindType == 8 && log_record.date_time >= FromTime && log_record.date_time <= ToTime)
            	|| (RewindType == 8 && log_record.date_time >= FromTime && ToTime == 0)
            	|| (RewindType == 6 && log_record.LogID >= FromTime && log_record.LogID <= ToTime)
            	|| (RewindType == 6 && log_record.LogID >= FromTime && ToTime == 0))
            	{
	         		if (!iOnlyRewindUnsentData || (iOnlyRewindUnsentData && log_record.HasBeenSent == 0))
               	{
               		is_uhf_code=0;
            			if(log_record.xpdr_code[0]==0x00)          //UHF code converted from string to long - byte 0 is always 0 if UHF code
            			{
                     	is_uhf_code=1;
               			for (b = 2; b <= 5; b++){
               		 		code = code << 8 | log_record.xpdr_code[b];
               			}
                        sprintf(sChip, "%lu", code);
                     }else{
                        sprintf(sChip, "%.6s", log_record.xpdr_code);
   							log_record.loop_data = ((log_record.loop_data >> 6) & 0x0F) + 1;

                     }

   				i = sprintf(buf, "%lu,%s,%lu,%03d,%d,%d,%d,%d,%d,%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
	            (unsigned long)0,
	            sChip,
	            log_record.date_time,
	            log_record.ms,
	            log_record.loop_data,
	            log_record.max_RSSI,
	            1, // Is rewind
	            1,
	            Settings.Channel+1,
	            0, 0, 0, 0, 0, 0, 0, 0,
	            0,
	            log_record.LogID
	            );


            		//	i = CreateSockString(log_record, 1, buf, code, is_uhf_code);
	         			sock_fastwrite(&MyServerSocket[iRewindSocket].ServerSocket, buf, i);
                     tcp_tick(NULL);
            		}
	         		iSent++;
	         		if (log_record.date_time > Settings.iLastTimeSent)
	         		{
	         			Settings.iLastTimeSent = log_record.date_time;
            		}
         		}
         		else if ((RewindType == 8 && ToTime != 0 && ToTime < log_record.date_time)
            		  	  ||
                 			(RewindType == 6 && ToTime != 0 && ToTime < log_record.LogID))
         		{
                  RWRState = RWR_STOPPED;
                  break;
         		}
         		iRecords++;
      			yield;
            }
   		}
   		while (i > 0);
   		//CloseNANDLogFile();
   		RWRState = RWR_STOPPED;
      }else{
         RWRState = RWR_STOPPED;
      }
      fat_Close(&RWR);
   }
}



// NOTE: The rewind commands can work with a time or a record number value
//       8: rewind using date/time
//       6: rewind using record number
//       9. rewind from remote server using date/time
void ActionRewindCommand(char *Buffer)
{
	char  *ptr;
   char RewindType;
   int x;

	ptr = Buffer;
   RewindType = Buffer[0] - 48; // 6 or 8 or 9
	ptr++; // Skip over the '8' or '6' character
	ptr++; // Skip over the split character (not used)
	ptr++; // Skip over the antenna character (not used)

   FromTime = strtol(ptr, NULL, 0);

   ToTime = 0; //zero this prior in case an ALL dump from RFIDServer
	while (*ptr != '\r')
   {
	   ptr++;
   } // Jump to the next CR chr
   ptr++; // Now jump over the CR chr

	ToTime = strtol(ptr, NULL, 0);


	iOnlyRewindUnsentData = 0;
   if (RewindType == 9)
   {
      RWRFromTime = FromTime;
      RWRToTime = ToTime;
		Remote_GetRecordNoByDate();
   }else{
   	RWRState = RWR_READING; //instruct to start rewinding
   }
	//RewindLogFile_BinarySearch(FromTime, ToTime, RewindType);
}



void SendDateTime(char SocketNo)
{
   unsigned long tm_now;
   char buf[64];

   /*    old code
   tm_now = read_rtc();
   mktm(&CurTime, tm_now);

   */
   mytime = RTC_Get();
   CurTime.tm_hour = mytime->hours;
  	CurTime.tm_min = mytime->minutes;
  	CurTime.tm_sec = mytime->seconds;
	CurTime.tm_wday = mytime->dow - 1;     //DS day 1 is Sunday, GPS is 0
	CurTime.tm_mday = mytime->day;
  	CurTime.tm_mon = mytime->month;
  	CurTime.tm_year = mytime->year + 100;    //DS is 2000, GPS is 1900
   sprintf(buf, "%.2d:%.2d:%.2d %.2d-%.2d-%d (%ld)\r\n",
   CurTime.tm_hour, CurTime.tm_min, CurTime.tm_sec,
   CurTime.tm_mday, CurTime.tm_mon, CurTime.tm_year + 1900, tm_now);
   sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, strlen(buf));
   tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
}



void SendReadingStatus(char SocketNo)
{
	char buf[5];
   buf[0] = 0x53; // S
   buf[1] = 0x3D; //
	if (ProgramState == READING)
   {
   	buf[2] = 0x31;
   }
   else
   {
   	buf[2] = 0x30;
   }
   if (iSendDataToPort)
   {
   	buf[3] = 0x31;
   }
   else
   {
   	buf[3] = 0x30;
   }
   buf[4] = 0x0A;
   sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, sizeof(buf));
   tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
}

// routine to communicate with nrf52833
// uses spi
// sends 1 byte only to set nrf52833 for next response

void comms_NRF(int cmd)
{

   char command, crc;
   char spi_recv_buff[255];
   char buf[20];
   char cmdi[2];
   char cmdb[8];
   char b, frame, n;
   int i,j;
   float batt;
   int batt_int, battpercent;
   char percent_str[5];
   char spi_finished;
   char loop_id;
   struct nrf_record txpdr_record[MAX_RECORDS];
   struct nrf_record *ptr_record;
   unsigned long boots, bt_time;
   char txpdr_fw_vers;
   int bytes_returned;


   CS1_ENABLE;
   for(i=0;i<256;i++);  //delay for enable
   //msDelay(1);
   cmdi[0] = cmd;
   cmdi[1] = 0;

	switch(cmd)
	{


      	case 0x01:    //poll for data available, if return = 1 then command 0x02
            spi_recv_buff[0] = 0;
            SPIWrRd(&cmdi, &spi_recv_buff, 2);    // 2 bytes for command and 2 return byte
            CS1_DISABLE;
            while(BitRdPortI(PBDR, 3)); //wait for it to go lo
            ret_records = spi_recv_buff[0];
            record_type = spi_recv_buff[1];

            if (ret_records == 0xBB){
             	printf("DEF - Ignored transaction from SPI");
               ret_records = 0;
               spi_record_state = 1;
               break;
            }
            if (ret_records > 40){
               printf("Records too much= %d\n\r", ret_records);
               spi_record_state = 1;
               ret_records = 0;
               break;
            }
            printf("returned records = %d\n\r", ret_records);
            spi_record_state = 2;
            break;

   		case 0x02:

         spi_record_state = 1; //reset this to wait for next records avail

         if(record_type == 1){

					SPIWrRd(&cmdi, &spi_recv_buff, (ret_records * REC_LENGTH) + 2);
               CS1_DISABLE;
               while(BitRdPortI(PBDR, 3)); //wait for it to go lo

               for(b=0;b < ret_records;b++)
               {
                  frame = REC_LENGTH * b;
                 	memcpy(&txpdr_record[b], &spi_recv_buff[frame+2], 10);
                  batt_int = (int)txpdr_record[b].battery;
						batt = (float)(batt_int)/100 + 1.8;
                  sprintf(percent_str, "%3fV", batt);
                  genieWriteStr(GENIE_TXPDR_BAT_STR, percent_str);
                  memcpy(&txpdr_record[b].loop_data, &spi_recv_buff[frame+12], 2);   //not sure where the 0 comes from between battery and loop data????? So have to do this
                  memcpy(&txpdr_record[b].xpdr_code,  &spi_recv_buff[frame+14], 6);
                  sprintf(code, "%.6s", txpdr_record[b].xpdr_code);
						genieWriteStr(GENIE_TXPDR_STR,  code);

                  chip_reads++;
                  genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_READS, chip_reads);
                  printf("code=%s\n\r", code);
                  //printf("code=%s, batt=%.2f wake=%x dat/tim=%lu ms=%u  loop=%u\n\r", code, batt, txpdr_record.wake_count, txpdr_record.date_time, txpdr_record.ms, txpdr_record.loop_data );
                  loop_id = ((txpdr_record[b].loop_data >> 6) & 0x0F) + 1;
                  //printf("timer ID = %d\n\r", loop_id);
                  if(txpdr_record[b].loop_data & 0x0E){
                  	printf("bt turned on setting = %d\n\r", txpdr_record[b].loop_data >> 1 & 0x07);
               	}
                  if(txpdr_record[b].loop_data & 0x400){
                  	printf("Playback response from TXPDR\n\r");
               	}
                  if(txpdr_record[b].loop_data & 0x01){
                   	printf("TXPDR put into low power mode\n\r");
                  }
                  iLastLogID++;
   					txpdr_record[b].LogID = iLastLogID;
                  //temporary fix to issue with ms over 999
                  //need to fix the nrf code for it later
                  if(txpdr_record[b].ms > 999){
                  	printf("milliseconds error %d", txpdr_record[b].ms);
                  	txpdr_record[b].ms = txpdr_record[b].ms - 1000;
                     txpdr_record[b].date_time = txpdr_record[b].date_time + 1;     //this might not be needed
                  }
                  ////////////
                  WriteChipToSocket(txpdr_record[b], 0, 0, 0);
                  if(Settings.Beeper) Beep();

               }

               printf("Records parsed = %d\n\r", ret_records);
               ptr_record = &txpdr_record;
               OpenNANDLogFile(1);
               SaveRAMToNAND(ptr_record, ret_records);
               CloseNANDLogFile();


           }else{

           	if (record_type == 2){

					bytes_returned = SPIWrRd(&cmdi, &spi_recv_buff, (ret_records * 7) + 8 + 9);
               CS1_DISABLE;
               //memset(txpdr_record, 0, sizeof(txpdr_record)*MAX_RECORDS);
               memcpy(&txpdr_record[0].xpdr_code,  &spi_recv_buff[2], 6);
               sprintf(code, "%.6s", txpdr_record[0].xpdr_code);
               printf("TXPDR has sent %d playback records for txpdr %s\n\r", ret_records, code);
               memcpy(&boots, &spi_recv_buff[8], 4);
               memcpy(&bt_time, &spi_recv_buff[12], 4);
               txpdr_fw_vers = spi_recv_buff[16];
               printf("Boots = %lu  BT Time = %lu  FW = %d\n\r",  boots, bt_time, txpdr_fw_vers);
               //write special header string before sending the rewind data
               for (j = 0; j < MAX_SOCKET_CONNECTIONS; j++)
   				{
               	if (MyServerSocket[j].ClientIsConnected)
         			{
                     //form is @code,records,boots,bt_time,fw
                     sprintf(buf, "@%s,%d,%lu,%lu,%d\n\r", code,ret_records,boots,bt_time,txpdr_fw_vers);
                     i = sock_fastwrite(&MyServerSocket[j].ServerSocket, buf, strlen(buf));
	         			tcp_tick(NULL);
         			}
               }
               for(b=0;b < ret_records;b++){
               	frame = 7 * b;
                  loop_id = spi_recv_buff[frame + 17] + 1;     // sent as 0...15
                  memcpy(&txpdr_record[b].xpdr_code, &txpdr_record[0].xpdr_code, 6);
                  txpdr_record[b].loop_data = (int)(((loop_id - 1) << 6) & 0x3C0);
               	memcpy(&txpdr_record[b].date_time,  &spi_recv_buff[frame + 18 ], 4);
                  memcpy(&txpdr_record[b].ms ,  &spi_recv_buff[frame + 22], 2);
               	printf("dat/tim=%lu ms=%u  loop=%u\n\r", txpdr_record[b].date_time, txpdr_record[b].ms, loop_id);
   					txpdr_record[b].LogID=0;
                  txpdr_record[b].max_RSSI=0;
                  txpdr_record[b].wake_count=0;
                  txpdr_record[b].battery=0;
                  WriteChipToSocket(txpdr_record[b], 0, 0, 0);
                  //chip_reads++;
                  genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_READS, chip_reads);
               }
               //OpenNANDLogFile(1);
               //SaveRAMToNAND(ret_records);
               //CloseNANDLogFile();
           	}
           }
           ret_records = 0;
           break;

      	case 0x03:    //set RF power of LF loop

            cmdi[1] = Settings.ReaderPower;          //increments of 10 (min 20 and max 100)
            SPIWrRd(cmdi, spi_recv_buff, 2);    // 2 bytes for command and 2 return byte
            SaveSettings();
            break;

      	case 0x04:    //set BT on time

          	cmdi[1] = RFID.bt_adv;         //0 = off, 1 = on
            SPIWrRd(cmdi, spi_recv_buff, 2);    // 2 bytes for command and 2 return byte
            //SPIWrite(&cmdi, 2);

            break;

      	case 0x05:		//program TXPDR code or activate DFU followed by a 0x06 command


            SPIWrRd(cmdi, spi_recv_buff, 2);    // 7 bytes for command and 1 return byte
            CS1_DISABLE;
            msDelay(1);
            //create a CRC for 7th byte - simple at this stage
            crc = 0xAA;
            for(b=0;b<6;b++){
            	crc = crc ^ RFID.chip_code[b];
            }
            memcpy(&cmdb[1], &RFID.chip_code, 6);
            cmdb[0] = 0x06;
            cmdb[7] = crc;
            CS1_ENABLE;
            SPIWrRd(cmdb, spi_recv_buff, 8);    // 8 bytes for command (1 cmd,6 code,1 crc) and 1 return byte
            sprintf(code, "%.6s", RFID.chip_code);
            printf("Written TXPDR code %s and CRC is %x \n\r", code, crc);
            encode_tag = 0;
            break;

      	case 0x07:		//set TXPDR to read back times

            cmdi[1] = RFID.playback;
            SPIWrRd(cmdi, spi_recv_buff, 2);    // 2 bytes for command and 2 return byte
            break;

      	case 0x08:		//sends date/time code followed by 0x09 code


            cmdi[1] = 0x00;
            SPIWrRd(cmdi, spi_recv_buff, 2);    // 2 bytes for command and 2 return byte
            CS1_DISABLE;
            msDelay(1);
            //nrftime = read_rtc();
            CurTime.tm_hour  = mytime->hours;
				CurTime.tm_min  =  mytime->minutes;
				CurTime.tm_year =  mytime->year + 100;
				CurTime.tm_mon = mytime->month;
				CurTime.tm_mday = mytime->day;
				CurTime.tm_wday = mytime->dow - 1;
				CurTime.tm_sec = mytime->seconds;
            nrftime = mktime(&CurTime);
            cmdb[0] = 0x09;
            cmdb[1] = (nrftime >> 24) & 0xFF;
				cmdb[2] = (nrftime >> 16) & 0xFF;
				cmdb[3] = (nrftime >> 8) & 0xFF;
				cmdb[4] = nrftime & 0xFF;
            CS1_ENABLE;
            for(i=0;i<256;i++);  //delay for enable
            SPIWrRd(cmdb, spi_recv_buff, 5);    // 5 bytes for command and 1 return byte
            msDelay(1);

            break;


      	case 0x0A:      //set station channel

            cmdi[1] = Settings.Channel;
            SPIWrRd(cmdi, spi_recv_buff, 2);    // 2 bytes for command and 2 return byte

            break;

      	case  0x0B:     //set txpdr to low power scanning mode

         	cmdi[1] = RFID.chip_sleep;
            SPIWrRd(cmdi, spi_recv_buff, 2);    // 2 bytes for command and 2 return byte
            break;

         case  0x0C:     //turn off the LF transmitter

            SPIWrRd(cmdi, spi_recv_buff, 2);
            break;

         case 0x0D:    //ask for li-ion battery ADC value

         	SPIWrRd(cmdi, spi_recv_buff, 2);
            CS1_DISABLE;
            batt_percent = spi_recv_buff[1];

            genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_BAT, batt_percent);
            genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_BAT, batt_percent);
            //shutdown if voltage too low to sustain 3.3V
            /*
            if(batt_percent < 5){
               command = MP_Read(MP2731_STATUS);
         		command &= 0x18;   //check if charging
         		if(command != 0x10) MP_Write(MP2731_BATFET, 0x61);    //turn batfet off hardware enabled, on time 0.5sec, off 10 sec
            }
            */
            break;

         case 0x0E:    //ask for fw version of nrf chip
            cmdi[1] = 0x00;
            SPIWrRd(cmdi, spi_recv_buff, 2);
            CS1_DISABLE;
            nrf52833_FW = spi_recv_buff[1];

         	break;

         case 0xFA:    //instruct tbe nrf52833 to go into DFU for 30 seconds

         	SPIWrRd(cmdi, spi_recv_buff, 2);

            break;

         default:
 	}

   //msDelay(2);
   CS1_DISABLE;
}






void ApplyTimeOffset (int offset){

   mytime = RTC_Get();
	CurTime.tm_hour  = mytime->hours;
	CurTime.tm_min  =  mytime->minutes;
	CurTime.tm_year =  mytime->year + 100;
	CurTime.tm_mon = mytime->month;
	CurTime.tm_mday = mytime->day;
	CurTime.tm_wday = mytime->dow - 1;
	CurTime.tm_sec = mytime->seconds;
	tm_wr(&CurTime);
   SEC_TIMER = mktime(&CurTime);
   mktm(&CurTime, mktime(&CurTime) + offset);
   tm_wr(&CurTime);


}

void SetDSTime (int offset)
{
   char date_string2[12];

   if(offset) ApplyTimeOffset(offset);    //for timezone change only

   //set DS time
   mytime->hours   = CurTime.tm_hour;
  	mytime->minutes = CurTime.tm_min;
  	mytime->seconds = CurTime.tm_sec;
  	mytime->dow     = CurTime.tm_wday + 1;     //DS day 1 is Sunday, GPS is 0
  	mytime->day     = CurTime.tm_mday;
  	mytime->month   = CurTime.tm_mon;
  	mytime->year    = CurTime.tm_year - 100;    //DS is 2000, GPS is 1900
   RTC_Set(mytime);
   set_time_nrf = 1;    //update the nrf time
   mytime = RTC_Get();
   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_MIN, mytime->minutes);
   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_HOUR, mytime->hours);
   sprintf(date_string2, "%02u-%02u-20%02u", mytime->day, mytime->month, mytime->year);
   genieWriteStr(GENIE_DATE_STR,  date_string2);

}

//------------------------------------------------------------------------
// Set Date and Time after command received from PC
//------------------------------------------------------------------------
void SetDateTimeX(char SocketNo, char *Buffer)
{
   unsigned long tm_now;
   char gen_buffer[64];


   CurTime.tm_hour = atoi(&Buffer[2]);
   CurTime.tm_min = atoi(&Buffer[5]);
   CurTime.tm_sec = atoi(&Buffer[8]);
   CurTime.tm_mday = atoi(&Buffer[11]);
   CurTime.tm_mon = atoi(&Buffer[14]);
   CurTime.tm_year = (atoi(&Buffer[17]) - 1900);
   CurTime.tm_wday = 0;

   tm_wr(&CurTime);
   SEC_TIMER = mktime(&CurTime);
   //set the DS3232 time - will not be exact until satellite PPS
   SetDSTime(0);

   //SyncMSTime();    //sync to exact millisecond

   // Confirm time update and respond to sender.
   tm_now = read_rtc();

   mktm(&CurTime, tm_now);

   sprintf(gen_buffer, "%02d:%02d:%02d %02d-%02d-%d (%ld)\r\n",
   CurTime.tm_hour, CurTime.tm_min, CurTime.tm_sec,
   CurTime.tm_mday, CurTime.tm_mon, CurTime.tm_year+1900, tm_now);
   sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, gen_buffer, strlen(gen_buffer));
   tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
}



int round_up_to_max_pow10(int n)
{
    int tmp;
    int i;

    tmp = n;
    i = 0;

    while ((tmp /= 10) >= 10) {
        i++;
    }

    if (n % (int)(pow(10, i + 1) + 0.5)) {
        tmp++;
    }

    for (; i >= 0; i--) {
        tmp *= 10;
    }

    return tmp;
}





//parses the IP address string input and does simple check that it is formmatted OK
int parseIP_check(char ip_type)
{
   char * token;
   int i;
   char istr[20];
   char temp_ip[4];

   memcpy(istr, keyboard_string, 20);
   token = strtok(istr, ".");
   i=0;
	while( token != NULL ) {
      temp_ip[i] = atoi(token);
      token = strtok(NULL, ".");
      i++;
   }
   if(i != 4){
   	return 0;
   }else{
   	if(ip_type == 1){
      	if(!Settings.useDHCP){
       		memcpy(Settings.RabbitIP, temp_ip, 4);

            printf("Bringing interface DOWN...\n");
         	ifdown(IF_ETH0);
            while(ifpending(IF_ETH0)!= IF_DOWN){
            	tcp_tick(NULL);
            }
            UpdateRabbitIP();
         }
      }
      if(ip_type == 2) memcpy(Settings.GPRSServerIP1, temp_ip, 4 );
      if(ip_type == 3) memcpy(Settings.RabbitGateway, temp_ip, 4 );
    	return 2;
   }
}





void updateGenie_Main(void)
{

   unsigned long ulTime;
   char date_string2[12];
   unsigned int max_register;
   float max_value;


   //added V1.19
   if(time_offset<0 || time_offset> 0){
   	Settings.TimeZone =  Settings.TimeZone + time_offset;
      SaveSettings();
      SetDSTime((time_offset*3600) + (Settings.Add30 * 1800));
   }
	///////////
   mytime = RTC_Get();
   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_MIN, mytime->minutes);
   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_HOUR, mytime->hours);
   sprintf(date_string2, "%02u-%02u-20%02u", mytime->day, mytime->month, mytime->year);
   genieWriteStr(GENIE_DATE_STR,  date_string2);

   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID2, Settings.Channel+1);
   genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_PWR, Settings.ReaderPower);
   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_POWER, Settings.ReaderPower);
   genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_POWER, Settings.ReaderPower);
   //UHF changes now strings on many objects
   genieWriteObject(GENIE_OBJ_STRINGS, GENIE_SYSTEM_STR, Settings.System);
   genieWriteObject(GENIE_OBJ_STRINGS, GENIE_LED1_STR, Settings.System);
   genieWriteObject(GENIE_OBJ_STRINGS, GENIE_LED2_STR, Settings.System);
   genieWriteObject(GENIE_OBJ_STRINGS, GENIE_LED3_STR, Settings.System);
   genieWriteObject(GENIE_OBJ_STRINGS, GENIE_LED4_STR, Settings.System);
   genieWriteObject(GENIE_OBJ_STRINGS, GENIE_BAT_OR_STATUS_STR, Settings.System);
   if(Settings.System){
   	 genieWriteObject(GENIE_OBJ_STRINGS,GENIE_TXPDR_BAT_STR , uhf_reading);
   }else{
      genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LPM, RFID.chip_sleep);
   }
   genieWriteStr(GENIE_TXPDR_STR,  code);
   time_offset = 0;
   if(board_vers>=32){
   	max_register = max_read(MAX17303_ADDRESS1, MAX17303_REPSOC);    //capacity %
   	max_value =  max_register * (float) 1/256;  //   in %
      genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_BAT, (int) max_value);
      genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_BAT, (int) max_value);
   }
}


void UHF_Reader_Control (int command){

    int filesize;

      if(command){
         ProgramState = READING;
         Open_Reader();
   		TM_InitialiseReader();
         uhf_reading = 1;
         //check nand and reformat if over 97% full
         filesize = (int) CheckNANDFileSize();
         if(filesize >= 97) ResetNANDLogFile();
         //double beep
      	BitWrPortI(PBDR, &PBDRShadow, 1, 2);
      	msDelay(50);
      	BitWrPortI(PBDR, &PBDRShadow, 0, 2);
      	msDelay(50);
      	BitWrPortI(PBDR, &PBDRShadow, 1, 2);
      	msDelay(50);
      	BitWrPortI(PBDR, &PBDRShadow, 0, 2);
         StartReaders();
         if(uhf_reading==1){
         	Settings.ShutDownStatus = ABNORMAL_SHUTDOWN;
         }else{
         	Settings.ShutDownStatus = NORMAL_SHUTDOWN;
         }
         SaveSettings();
         uhf_duty_cycle = 1;
         genieActivateForm(GENIE_FORM_MAIN);
         updateGenie_Main();
      }else{
      	ProgramState = IDLE;
    		BitWrPortI ( PBDR, &PBDRShadow, 1, 6 );       //solid green
         StopReaders();
         Settings.ShutDownStatus = NORMAL_SHUTDOWN;
         SaveSettings();
         uhf_reading = 0;
         BitWrPortI(PBDR, &PBDRShadow, 1, 2);
   		msDelay(50);
   		BitWrPortI(PBDR, &PBDRShadow, 0, 2);
         genieWriteContrast(Settings.Brightness);
         genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LOOP, 0);
   		genieWriteObject(GENIE_OBJ_LED, GENIE_LED_PBACK, 0);
   		genieWriteObject(GENIE_OBJ_LED, GENIE_LED_BTON, 0);
   		genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LPM, 0);
      }
}

//*********************************************************
//main event handler for the 4D Display running Genie
//uses GENIE2.LIB
//**********************************************************

void myGenieEventHandler(void) {

	char indata;
   char characters[2] = {'\0','\0'};
   char str_digits;
   int string_length;
   int dat, j;
   char date_string2[12];
   char date_string3[26];
   char char_string[20];
   char buf[35];
   char macaddress[9];
   unsigned long ulTime;
   struct tm time_struc;
   long iTime;
   int filesize;

	genieFrame Event;
   genieDequeueEvent(&Event);
   LastTouchTime = SEC_TIMER;
	/* If the commamd received is from a Reported Event, it will be processed here. */
	if (Event.reportObject.cmd == GENIE_REPORT_EVENT) {

      //FORMS
	   if (Event.reportObject.object == GENIE_OBJ_FORM) {
         if(Event.reportObject.index == GENIE_FORM_TIME){
            genieWriteObject(GENIE_OBJ_SLIDER, GENIE_GPS, 1); //always on for now
            if(Settings.TimeZone < 0){
             	characters[0] = '-';
            }else{
               characters[0] = '+';
            }
          	genieWriteStr(GENIE_SIGN_STR, characters);
            genieWriteObject(GENIE_OBJ_TRACKBAR, GENIE_TB_TIMEZ, Settings.TimeZone + 12);
            genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_TIMEZONE, abs(Settings.TimeZone));
            genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_GPS, Settings.AutoSetGPSTime);
            genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_ADD30, Settings.Add30);
            last_winbutton = GENIE_BUTTON_BACK4;
         //removed to MAIN because UHF version opens Main instead of Settings
         /*
         }else if(Event.reportObject.index == GENIE_FORM_SETTINGS){

         	//////////////
         	//check if timezone and/or add30 have changed
             //set the time if timezone changed
            if(last_winbutton == GENIE_BUTTON_BACK4){
         		if(time_offset<0 || time_offset> 0){
              		Settings.TimeZone =  Settings.TimeZone + time_offset;
               	SaveSettings();
            		SetDSTime((time_offset*3600) + (Settings.Add30 * 1800));
               }
            }
         */

         }else if(Event.reportObject.index == GENIE_FORM_MAIN){
             updateGenie_Main();

            //also last read txdpr code
         }else if(Event.reportObject.index == GENIE_FORM_RFID){
         	diag_visible = 0;

         }else if(Event.reportObject.index == GENIE_FORM_ANT){
         	diag_visible = 1;

         }else if(Event.reportObject.index == GENIE_FORM_UHF){


         }else if(Event.reportObject.index == GENIE_FORM_NETWORKING){

            updateNetworkStrings();

            last_genieform = GENIE_FORM_NETWORKING;
         }else if(Event.reportObject.index == GENIE_FORM_DATA){
         	sprintf(char_string, "%lu", iLastLogID);
            genieWriteStr(GENIE_RECORDS_STR, char_string);
            //genieWriteStr(GENIE_CODE_STR, txpdr_string);
            mktm(&time_struc, Settings.iLastTimeSent);
            sprintf(date_string3, "%02u:%02u:%02u   %02u-%02u-20%02u", time_struc.tm_hour, time_struc.tm_min, time_struc.tm_sec, time_struc.tm_mday, time_struc.tm_mon, time_struc.tm_year - 100);
            genieWriteStr(GENIE_LAST_READ_STR, date_string3);
            genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_FILE, (int)CheckNANDFileSize());

         }else if(Event.reportObject.index == GENIE_FORM_OTHER){
          	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_BUZZER, Settings.Beeper);
            genieWriteObject(GENIE_OBJ_SLIDER, GENIE_SLIDER_DIM, Settings.Brightness);
            genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_DIM, Settings.Dim);
            genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_TRIGGER, 0);
            genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_SYSTEM, Settings.System);
            sprintf(char_string, "V%d.%d", _FIRMWARE_VERSION_ >> 8,  _FIRMWARE_VERSION_ & 0xFF);
            genieWriteStr(GENIE_INSTFW_STR, char_string);
            genieWriteStr(GENIE_FW_PROGRESS_STR, "");

         }
      //Userbuttons (Sleep Form only)
      }else if(Event.reportObject.object == GENIE_OBJ_USERBUTTON){

           genieWriteContrast(Settings.Brightness);
           lo_backlight = 0;
           genieActivateForm(GENIE_FORM_MAIN);
           genieWriteStr(GENIE_TXPDR_STR,  code);
           trigger_dim = 1;    //we will only be here if dim is on, arm for next dim timer event
      //KNOBS
	   }else if (Event.reportObject.object == GENIE_OBJ_KNOB) { // If the Reported Message was from a Slider
           //set and save RFID power
            /*
            if (indata < 20){
            	indata = 20;
            }else{
            	indata = round_up_to_max_pow10(indata);
            }
            */
            indata = genieGetEventData( & Event);
            Settings.ReaderPower = indata;
            //set the values
            genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_POWER, Settings.ReaderPower);
   			genieWriteObject(GENIE_OBJ_KNOB, GENIE_KNOB_PWR, Settings.ReaderPower);
   			genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_POWER_CHANGE, Settings.ReaderPower);
   			genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_PWR, Settings.ReaderPower);
            comms_NRF(0x03);

       //TRACKBARS
       }else if (Event.reportObject.object == GENIE_OBJ_TRACKBAR) {
         if(Event.reportObject.index == GENIE_TB_ID){ //set and save box ID
         		indata = genieGetEventData( & Event);
            	Settings.Channel = indata;
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID, Settings.Channel+1);
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID2, Settings.Channel+1);
               comms_NRF(0x0A);
               SaveSettings();
         }else if (Event.reportObject.index == GENIE_TB_ID2){
               indata = genieGetEventData( & Event);
            	Settings.Channel = indata;
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID, Settings.Channel+1);
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID1, Settings.Channel+1);
               SaveSettings();
   		}else if (Event.reportObject.index == GENIE_TB_BT_ON){
         		indata = genieGetEventData( & Event);
            	RFID.bt_adv = indata;    //set and save bluetooth on time
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_BT_TIME_ON, BT_ON_TIMES[indata]);
               if(BT_ON_TIMES[indata]){
                 genieWriteObject(GENIE_OBJ_LED, GENIE_LED_BTON, 1);
               }else{
                 genieWriteObject(GENIE_OBJ_LED, GENIE_LED_BTON, 0);
               }
               comms_NRF(0x04);
         }else if (Event.reportObject.index == GENIE_TB_TIMEZ){
             //set and save timezone -12 to +12
         	dat = Event.reportObject.data_lsb - 12;
            //time_offset = time_offset + dat - Settings.TimeZone;
            time_offset = dat - Settings.TimeZone;
            if(dat<0){
            	dat = abs(dat);
            	characters[0] = '-';
            }else{
                characters[0] = '+';
            }
            genieWriteStr(GENIE_SIGN_STR, characters);
            genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_TIMEZONE, dat);
         }
       //4DBUTTONS
       }else if (Event.reportObject.object == GENIE_OBJ_4DBUTTON){
         indata = genieGetEventData( & Event);
         if (Event.reportObject.index == GENIE_PLAYBACK){
            RFID.playback = indata;
            genieWriteObject(GENIE_OBJ_LED, GENIE_LED_PBACK, RFID.playback);
            comms_NRF(0x07);
         }else if(Event.reportObject.index == GENIE_SLEEP){
            RFID.chip_sleep = indata;
            genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LPM, RFID.chip_sleep);
            comms_NRF(0x0B);
         }else if(Event.reportObject.index == GENIE_DHCP){
            if(indata){
            	//DHCP activated
               Settings.useDHCP = 1;
            }else{
               //default static IP
               Settings.useDHCP = 0;
            }
            ifdown(IF_ETH0);
            while(ifpending(IF_ETH0)!= IF_DOWN){
            	tcp_tick(NULL);
            }
         	UpdateRabbitIP();
            SaveSettings();
         }else if(Event.reportObject.index == GENIE_REMOTE){
           max_write(MAX17303_ADDRESS1, MAX17303_FAULTS, 0x00);
           if(indata){
            	//Remote activated
               if(!Settings.RemoteType){      //only change if was not previously turned on
                  Settings.RemoteType = 2;
               }
            }else{
               //default
               //Remote_Disconnect();
               Settings.RemoteType = 0;
               Toggle_Modem();
            }

            SaveSettings();
         }else if(Event.reportObject.index == GENIE_BUZZER){
             Settings.Beeper = indata;
             SaveSettings();
         }else if(Event.reportObject.index == GENIE_DIM){
         	Settings.Dim = indata;
            SaveSettings();
            trigger_dim = indata;
         }else if(Event.reportObject.index == GENIE_TRIGGER){
             Settings.TriggerOn = indata;
             if(indata){
             	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_TRIGGER, 0);  //disabling trigger as there is no circuit now for it in UHF version
   				//WrPortI(I1CR, &I1CRShadow, 0x25);		// enable external INT1 on PE5 and PE1, falling edge PE1, rising edge PE5, priority 1
             }else{
             	WrPortI(I1CR, &I1CRShadow, 0x05);		// enable external INT1 on PE1 only, falling edge, priority 1
             }
             SaveSettings();
         }else if(Event.reportObject.index == GENIE_ADD30){
            Settings.Add30 = indata;
           	SaveSettings();             ;
         }else if(Event.reportObject.index == GENIE_GPS){
         	//always on for now so can't change
            Settings.AutoSetGPSTime = indata;
            SaveSettings();
         }else if(Event.reportObject.index == GENIE_RFID){
         	if(Settings.System){
            	genieActivateForm(GENIE_FORM_UHF);
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID1, Settings.Channel+1);
            	genieWriteObject(GENIE_OBJ_TRACKBAR, GENIE_TB_ID2, Settings.Channel);
            	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_UHF_READ, uhf_reading);
            	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_UHF_MODE, uhf_mode);
               genieWriteObject(GENIE_OBJ_STRINGS, GENIE_COUNTRY_STR, Settings.UHF_region);
              	genieWriteObject(GENIE_OBJ_STRINGS, GENIE_FREQ_STR, Settings.UHF_region);
            }else{
               genieActivateForm(GENIE_FORM_RFID);
               genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_POWER_CHANGE, Settings.ReaderPower);
   				genieWriteObject(GENIE_OBJ_KNOB, GENIE_KNOB_PWR, Settings.ReaderPower);
            	genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_ID, Settings.Channel+1);
            	genieWriteObject(GENIE_OBJ_TRACKBAR, GENIE_TB_ID, Settings.Channel);
            	genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_BT_TIME_ON, BT_ON_TIMES[RFID.bt_adv]);
            	genieWriteObject(GENIE_OBJ_TRACKBAR, GENIE_TB_BT_ON, RFID.bt_adv);
            	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_PLAYBACK, RFID.playback);
            	genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_SLEEP, RFID.chip_sleep);
            }
         }else if(Event.reportObject.index == GENIE_SYSTEM){
         	//UHF or Active toggle
            if(indata==0){  //switched off UHF from Other Toggle
            	serEclose;
   				BitWrPortI ( PBDR, &PBDRShadow, 1, 4);    //PB4 Hi   - turn off reader
               comms_NRF(0x0A);    //set the schannel
               comms_NRF(0x03);    //set the LF power on the nrf52833
            }else{
               BitWrPortI ( PBDR, &PBDRShadow, 0, 4);    //PB4 Hi   - turn on reader
               comms_NRF(0x0C);    // turn of LF transmitter
               Open_Reader();
            }
            Settings.System = indata;
            SaveSettings();
         }else if(Event.reportObject.index == GENIE_UHF_READ){
            UHF_Reader_Control(indata);
         }else if(Event.reportObject.index==GENIE_UHF_MODE){
         	uhf_mode = indata;
            //will be set on next start read
         }
       //SLIDER
       }else if (Event.reportObject.object == GENIE_OBJ_SLIDER){
            indata = genieGetEventData( & Event);
            if(indata < 1){
            	 indata = 1;
                genieWriteObject(GENIE_OBJ_SLIDER, GENIE_SLIDER_DIM, indata);
            }
            Settings.Brightness = indata;
            SaveSettings();
            genieWriteContrast(indata);

       //WINBUTTONS
       }else if (Event.reportObject.object == GENIE_OBJ_WINBUTTON){


         if(Event.reportObject.index == GENIE_BUTTON_CODE){
         	last_winbutton = GENIE_BUTTON_CODE;
         	if(admin){
         	   //only used for programming transponders and dfu
               //comment this out for everybody else as only use in factory!!
         		//transmitter_off = 1;
               comms_NRF(0x0C);
           		strcpy(keyboard_string, txpdr_string);
           		genieActivateForm(GENIE_FORM_KEYBOARD);
           		if(keyboard_string[5]=='9'){
           			keyboard_string[4]  += 1;
               	keyboard_string[5] = '0';
           		}else{
           			keyboard_string[5] += 1;
           		}
           		genieWriteStr(GENIE_KB_STR, keyboard_string);
            }else{
               genieActivateForm(GENIE_FORM_KEYPAD);
               genieWriteStr(GENIE_KP_STR, "Enter admin pin");
               sprintf(keyboard_string, "\0");
               enter_pin = 1;
               //last_winbutton = 0; //reset this
            }
         }else if(Event.reportObject.index == GENIE_BUTTON_UHF_CHANGE){
            msDelay(300);
            genieActivateForm(GENIE_FORM_KEYPAD);
            if(admin){
            	//Allow the region code selection

               sprintf(keyboard_string, "%d", Settings.UHF_region + 1 );
               genieWriteStr(GENIE_KP_STR, keyboard_string);
            }else{
               genieActivateForm(GENIE_FORM_KEYPAD);
               genieWriteStr(GENIE_KP_STR, "Enter admin pin");
               sprintf(keyboard_string, "\0");
               enter_pin = 1;
            }
            last_winbutton = GENIE_BUTTON_UHF_CHANGE;

         }else if(Event.reportObject.index == GENIE_BUTTON_RESET){
          	genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_READS, 0);
            chip_reads = 0;
         }else if(Event.reportObject.index == GENIE_BUTTON_SETREMOTE_IP){
          	last_winbutton = GENIE_BUTTON_SETREMOTE_IP;
            genieActivateForm(GENIE_FORM_KEYPAD);
            sprintf(keyboard_string, "%d.%d.%d.%d", Settings.GPRSServerIP1[0], Settings.GPRSServerIP1[1], Settings.GPRSServerIP1[2], Settings.GPRSServerIP1[3]);
            genieWriteStr(GENIE_KP_STR, keyboard_string);
         }else if(Event.reportObject.index == GENIE_BUTTON_SETGATEWAY_IP){
          	last_winbutton = GENIE_BUTTON_SETGATEWAY_IP;
            genieActivateForm(GENIE_FORM_KEYPAD);
            sprintf(keyboard_string, "%d.%d.%d.%d", Settings.RabbitGateway[0], Settings.RabbitGateway[1], Settings.RabbitGateway[2], Settings.RabbitGateway[3]);
            genieWriteStr(GENIE_KP_STR, keyboard_string);
         }else if(Event.reportObject.index == GENIE_BUTTON_SETREMOTE_PORT){
            last_winbutton = GENIE_BUTTON_SETREMOTE_PORT;
            genieActivateForm(GENIE_FORM_KEYPAD);
            sprintf(keyboard_string, "%d", Settings.GPRSServerPort);
            genieWriteStr(GENIE_KP_STR, keyboard_string);
         }else if(Event.reportObject.index == GENIE_BUTTON_SETAPN){
            last_winbutton = GENIE_BUTTON_SETAPN;
            strcpy(keyboard_string, Settings.APNName);
            genieActivateForm(GENIE_FORM_KEYBOARD);
            genieWriteStr(GENIE_KB_STR, keyboard_string);
         }else if(Event.reportObject.index == GENIE_BUTTON_SETLAN){
            last_winbutton = GENIE_BUTTON_SETLAN;
            genieActivateForm(GENIE_FORM_KEYPAD);
            sprintf(keyboard_string, "%d.%d.%d.%d", Settings.RabbitIP[0], Settings.RabbitIP[1], Settings.RabbitIP[2], Settings.RabbitIP[3]);
            genieWriteStr(GENIE_KP_STR, keyboard_string);
         }else if(Event.reportObject.index == GENIE_BUTTON_CANCEL){
         	genieActivateForm(GENIE_FORM_MAIN);
            updateGenie_Main;
         }else if(Event.reportObject.index == GENIE_BUTTON_CLEAR_LOG){
            last_winbutton = GENIE_BUTTON_CLEAR_LOG;
            // open keyboard
            genieActivateForm(GENIE_FORM_KEYBOARD);
            genieWriteStr(GENIE_KB_STR, "WARNING 'Y' to delete");

          }else if(Event.reportObject.index == GENIE_BUTTON_CHECK){

               new_firmware_avail = check_fw();
               genieWriteStr(GENIE_FW_PROGRESS_STR, "");
               if(new_firmware_avail >= 0){    //-1 error, 0 no update, 1 new fw
                  sprintf(char_string, "V%d.%d", new_fw_vers >> 8,  new_fw_vers & 0xFF);
            		genieWriteStr(GENIE_CURRFW_STR, char_string);
                  if(new_firmware_avail==1) genieWriteStr(GENIE_FW_PROGRESS_STR, "New Update Available");
               }
          }else if(Event.reportObject.index == GENIE_BUTTON_UPDATE_FIRMWARE){
               //put the nrf52833 into DFU ****Temporary
               comms_NRF(0xFA);
               // Find a better place to trigger a DFU!!!!!!!!!!!
          		if(new_firmware_avail) install_firmware();
          }else if (Event.reportObject.index == GENIE_BUTTON_CHECK_RL){
              if(!uhf_reading){      //only do a test if we are not reading
                  for(j=1;j<5;j++) genieWriteObject(GENIE_OBJ_GAUGE, j+2, 0);     //clear bar gauges
                  diag_visible = 1;
              		Open_Reader();
   			  		TM_InitialiseReader();
              }
          }else if (Event.reportObject.index == GENIE_BUTTON_DIAGNOSTICS){
               diag_visible = 1;
          }

       //KEYBOARD
       }else if (Event.reportObject.object == GENIE_OBJ_KEYBOARD){
         //Keyboard
         if(Event.reportObject.index == GENIE_KEYBOARD){
            if(last_winbutton == GENIE_BUTTON_CLEAR_LOG){
                 if(Event.reportObject.data_lsb == 0x59 || Event.reportObject.data_lsb == 0x79){
                        //y or Y pressed...Allow file delete
                        ResetNANDLogFile();
                 }
                 genieWriteObject(GENIE_OBJ_STRINGS, GENIE_RECORDS_STR, iLastLogID);
            	  genieWriteObject(GENIE_OBJ_STRINGS, GENIE_LAST_READ_STR, 0);
            	  genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_FILE, 0);   //reset to 0
                 genieActivateForm(GENIE_FORM_DATA);
         	}
         	else if(Event.reportObject.data_lsb == '\r'){
            	if(last_winbutton == GENIE_BUTTON_SETAPN){
                  strcpy(Settings.APNName, keyboard_string);
                  SaveSettings();
                  genieActivateForm(GENIE_FORM_NETWORKING);
                  updateNetworkStrings();
               }else if(last_winbutton == GENIE_BUTTON_CODE){
                  strncpy(txpdr_string, keyboard_string, 6);  //6 characters only allowed
                  memcpy(RFID.chip_code, txpdr_string, 6);
                  genieActivateForm(GENIE_FORM_DATA);
                  genieWriteStr(GENIE_CODE_STR, txpdr_string);
                  encode_tag = 1;
               }
         	}else if(Event.reportObject.data_lsb == 0x08){   //back
            	string_length = strlen(keyboard_string) - 1;
            	if(string_length >= 0){
            		keyboard_string[string_length] = '\0';
            		genieWriteStr(GENIE_KB_STR, keyboard_string);
               }
            }else{
            	characters[0] = Event.reportObject.data_lsb;
            	if(strlen(keyboard_string) < 29){          //29 characters only allowed
            			strncat(keyboard_string, characters, 1);
            			genieWriteStr(GENIE_KB_STR, keyboard_string);
               }
         	}
         //KEYPAD
         }else if (Event.reportObject.index == GENIE_KEYPAD){
            if(Event.reportObject.data_lsb == 11){       //back
               string_length = strlen(keyboard_string) - 1;
            	if(string_length >= 0){
            		keyboard_string[string_length] = '\0';
            		genieWriteStr(GENIE_KP_STR, keyboard_string);
               }
            }else if(Event.reportObject.data_lsb == 12){   //enter

               if(last_winbutton == GENIE_BUTTON_SETLAN){
               	//parse the IP and check
                  if(parseIP_check(1)){
                     genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_DHCP, 0);
                     Settings.useDHCP = 0;
                  }
               }else if(last_winbutton == GENIE_BUTTON_SETREMOTE_IP){
                  //parse the IP and check
                  parseIP_check(2);
               }else if(last_winbutton == GENIE_BUTTON_SETREMOTE_PORT){
                  Settings.GPRSServerPort = atoi(keyboard_string);
               }else if(last_winbutton == GENIE_BUTTON_SETGATEWAY_IP){
               	parseIP_check(3);
               }else if(enter_pin){
                  if(atoi(keyboard_string) == 6868) admin = 1;   //pin correct, now allow code/dfu setting
                  enter_pin = 0;
                  if(last_winbutton == GENIE_BUTTON_CODE){
                  	genieActivateForm(GENIE_FORM_DATA);
                  }else if(last_winbutton == GENIE_BUTTON_UHF_CHANGE){
                  	genieActivateForm(GENIE_FORM_UHF);
                  }
                  return;
               }
               if(last_winbutton == GENIE_BUTTON_UHF_CHANGE){
               	if(atoi(keyboard_string) < 7){     //for now just 6 regions 0-4
                  	Settings.UHF_region = atoi(keyboard_string) - 1;
                    	SaveSettings();
                  }
                  genieActivateForm(GENIE_FORM_UHF);
                  genieWriteObject(GENIE_OBJ_STRINGS, GENIE_COUNTRY_STR, Settings.UHF_region);
              		genieWriteObject(GENIE_OBJ_STRINGS, GENIE_FREQ_STR, Settings.UHF_region);
                  return;
               }
               if(last_winbutton == GENIE_BUTTON_CODE){
                  genieActivateForm(GENIE_FORM_DATA);
                  return;
               }else{
                   genieActivateForm(GENIE_FORM_NETWORKING);
               }
               updateNetworkStrings();
               SaveSettings();
            }else{                                              //a number or '.'
               characters[0] = (char)Event.reportObject.data_lsb;
            	if(strlen(keyboard_string) < 21){          //12 characters only allowed
            		strncat(keyboard_string, characters, 1);
            		genieWriteStr(GENIE_KP_STR, keyboard_string);
            	}
            }
         }
       }


	/* If the commamd received is from a Reported Object, which occurs if a Read Object (genie.ReadOject) is requested in the main code, reply processed here. */
	}else if (Event.reportObject.cmd == GENIE_REPORT_OBJ) {
	    if (Event.reportObject.object == GENIE_OBJ_USER_LED) { // If the Reported Message was from a User LED
	        /*if (Event.reportObject.index == 0) { // If UserLed0 (Index = 0)
	            bool UserLed0_val = genieGetEventData( & Event); // Receive the event data from the UserLed0
	            UserLed0_val = !UserLed0_val; // Toggle the state of the User LED Variable
	            genieWriteObject(GENIE_OBJ_USER_LED, 0, UserLed0_val); // Write UserLed0_val value back to to UserLed0
	        }
           */
	    }

	/********** This can be expanded as more objects are added that need to be captured *************
	*************************************************************************************************
	Event.reportObject.cmd is used to determine the command of that event, such as an reported event
	Event.reportObject.object is used to determine the object type, such as a Slider
	Event.reportObject.index is used to determine the index of the object, such as Slider0
	genieGetEventData(&Event) us used to save the data from the Event, into a variable.
	*************************************************************************************************/
	}else if (Event.reportObject.cmd == GENIE_PING) {
	    if (Event.reportObject.object == GENIE_DISCONNECTED) {


	    } else if (Event.reportObject.object == GENIE_READY) {
	        /* This function runs once, when the LCD is connected and synchronized.
	        You may use this to restore screen widgets, or process other code. */
	    	//genieWriteObject(GENIE_OBJ_LED_DIGITS, 0, slider_val); // Restore Leddigits0
	        //genieWriteObject(GENIE_OBJ_SLIDER, 0, slider_val); // Restore Slider0
	        static int recover_times = -1; // how many times did the display recover?
	        recover_times++;
//	        genieWriteStr(0, (String) GENIE_VERSION + "\n\n\tRecovered " + recover_times + " Time(s)!"); // Restore text in Strings0
	    } else if (Event.reportObject.object == GENIE_ACK) {

	    } else if (Event.reportObject.object == GENIE_NAK) {

	    }
	}
}



void TCPIPClose(char SocketNo)
{
   sock_close(&MyServerSocket[SocketNo].ServerSocket);

}

void TCPIPCloseSockets()
{
	int i;
   for (i = 0; i < MAX_SOCKET_CONNECTIONS; i++)
   {
      TCPIPClose(i);
      iClientType[i] = 0;
   }
   sock_close(&ResetSocket.ServerSocket);
}

void TCPIPOpen(char SocketNo)
{
	tcp_listen(&MyServerSocket[SocketNo].ServerSocket, DATA_PORT, 0, 0, NULL, 0);
   genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 0);
   iClientType[SocketNo] = 0;
}


int TCPIPOpenSockets()
{
	int i;
   for (i = 0; i < MAX_SOCKET_CONNECTIONS; i++)
   {
      TCPIPOpen(i);
   }
	tcp_listen(&ResetSocket.ServerSocket, RESET_PORT, 0, 0, NULL, 0);
   udp_extopen(&UDPSocket.ServerSocket, IF_ETH0, 2000, -1L, -1, NULL, 0, 0);
}



/*
  This is used in conjunction with Settings.DataOnRequest
  It must only run while reading !!!
  The rewind will only send records that haven't been sent: logEntry.HasBeenSent
*/
void StartDataSend(char *Buffer)
{
	char  *ptr;

	ptr = Buffer;
	ptr++; // Skip over the '8' character
	ptr++; // Skip over the split character (not used)
	ptr++; // Skip over the antenna character (not used)
	RWRFromTime = strtol(ptr, NULL, 0);
   if (RWRFromTime == 0)
   {
   	RWRFromTime = Settings.iLastTimeSent;
   }
   RWRToTime = 0;
   iOnlyRewindUnsentData = 1;
   iSendDataToPort = 1;
   FromTime = RWRFromTime;
   ToTime = 0;
	//RewindLogFile_BinarySearch(RWRFromTime, 0, 8);
   RWRState = RWR_READING;
}



void SendSetting_Int(char SocketNo, int SettingNo, int Setting)
{
	char buf[100];

	sprintf(buf, "U%c%d\n", SettingNo, Setting);
	sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, strlen(buf));
	tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
}

void SendSetting_LongInt(char SocketNo, int SettingNo, unsigned long Setting)
{
	char buf[100];

	sprintf(buf, "U%c%lu\n", SettingNo, Setting);
	sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, strlen(buf));
	tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
}

void SendSetting_Str(char SocketNo, int SettingNo, char *Setting)
{
	char buf[100];

	sprintf(buf, "U%c%s\n", SettingNo, Setting);
	sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, strlen(buf));
	tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
}


//RFIDServer and Outreach local settings
//Added back for Outreach functionality
void SendSettings(char SocketNo)
{
	char sIP[20];
	unsigned long iFileSize;

	SendSetting_Int(SocketNo, 0x01, Settings.RemoteType);

	sprintf(sIP, "%d.%d.%d.%d", Settings.GPRSServerIP1[0], Settings.GPRSServerIP1[1], Settings.GPRSServerIP1[2], Settings.GPRSServerIP1[3]);
	SendSetting_Str(SocketNo, 0x02, sIP);
	SendSetting_Int(SocketNo, 0x03, Settings.GPRSServerPort);
	SendSetting_Str(SocketNo, 0x04, Settings.APNName);
	SendSetting_Str(SocketNo, 0x05, Settings.APNUser);
	SendSetting_Str(SocketNo, 0x06, Settings.APNPassword);
	//SendSetting_Int(SocketNo, 0x07, Settings.RegRegion);
	//SendSetting_Int(SocketNo, 0x08, Settings.CommsProtocol);
	SendSetting_Int(SocketNo, 0x09, Settings.OutputType);
	// Don't use 0x10 or 0x11!! (CR/LF)
   /*
	SendSetting_Int(SocketNo, 0x0C, Settings.Ant11);
	SendSetting_Int(SocketNo, 0x0D, Settings.Ant12);
	SendSetting_Int(SocketNo, 0x0E, Settings.Ant13);
	SendSetting_Int(SocketNo, 0x0F, Settings.Ant14);
	SendSetting_Int(SocketNo, 0x10, Settings.Ant21);
	SendSetting_Int(SocketNo, 0x11, Settings.Ant22);
	SendSetting_Int(SocketNo, 0x12, Settings.Ant23);
	SendSetting_Int(SocketNo, 0x13, Settings.Ant24);
	SendSetting_Int(SocketNo, 0x14, Settings.ReaderMode[0]);
	SendSetting_Int(SocketNo, 0x15, Settings.ReaderMode[1]);
	SendSetting_Int(SocketNo, 0x16, Settings.ReaderSession[0]);
	SendSetting_Int(SocketNo, 0x17, Settings.ReaderSession[1]);
	SendSetting_Int(SocketNo, 0x18, Settings.ReaderPower[0]);
	SendSetting_Int(SocketNo, 0x19, Settings.ReaderPower[1]);

	sprintf(sIP, "%d.%d.%d.%d", Settings.Reader1IP[0], Settings.Reader1IP[1], Settings.Reader1IP[2], Settings.Reader1IP[3]);
	SendSetting_Str(SocketNo, 0x1A, sIP);
	sprintf(sIP, "%d.%d.%d.%d", Settings.Reader2IP[0], Settings.Reader2IP[1], Settings.Reader2IP[2], Settings.Reader2IP[3]);
	SendSetting_Str(SocketNo, 0x1B, sIP);
	// Don't read NAND file while reading or it affects normal writing

	iFileSize = 0;
	SendSetting_LongInt(SocketNo, 0x1C, iFileSize / sizeof(struct TlogEntry));
	SendSetting_Int(SocketNo, 0x1D, Settings.GatingMode);
   */
	SendSetting_LongInt(SocketNo, 0x1E, 3);  //gating set at 3 always
	SendSetting_Int(SocketNo, 0x1F, Settings.Channel);
	SendSetting_Int(SocketNo, 0x20, 0);
	SendSetting_Int(SocketNo, 0x21, Settings.Beeper);
	SendSetting_Int(SocketNo, 0x22, Settings.AutoSetGPSTime);
	SendSetting_Int(SocketNo, 0x23, Settings.TimeZone);
	SendSetting_Int(SocketNo, 0x24, Settings.DataOnRequest);
	SendSetting_Int(SocketNo, 0x25, Settings.Channel);
	//SendSetting_Int(SocketNo, 0x26, Settings.Ant4IsBackup[0]);
	//SendSetting_Int(SocketNo, 0x27, Settings.Ant4IsBackup[1]);
	//SendSetting_Int(SocketNo, 0x28, Settings.BeepOnFirstTimeSeen);
	//SendSetting_Str(SocketNo, 0x29, Settings.LANServer);
	sprintf(sIP, "%d.%d.%d.%d", Settings.RabbitGateway[0], Settings.RabbitGateway[1], Settings.RabbitGateway[2], Settings.RabbitGateway[3]);
	SendSetting_Str(SocketNo, 0x2A, sIP);
	sprintf(sIP, "%d.%d.%d.%d", Settings.RabbitDNS[0], Settings.RabbitDNS[1], Settings.RabbitDNS[2], Settings.RabbitDNS[3]);
	SendSetting_Str(SocketNo, 0x2B, sIP);
	// The following is just here for completeness. The socket client knows the IP address anyway.
	sprintf(sIP, "%d.%d.%d.%d", Settings.RabbitIP[0], Settings.RabbitIP[1], Settings.RabbitIP[2], Settings.RabbitIP[3]);
	SendSetting_Str(SocketNo, 0x2C, sIP);
	SendSetting_Int(SocketNo, 0x2E, Settings.SendDataToRemoteServer);
	sprintf(sIP, "%d.%d.%d.%d", Settings.GPRSServerIP2[0], Settings.GPRSServerIP2[1], Settings.GPRSServerIP2[2], Settings.GPRSServerIP2[3]);
	SendSetting_Str(SocketNo, 0x30, sIP);
	sprintf(sIP, "%d.%d.%d.%d", Settings.GPRSServerIP3[0], Settings.GPRSServerIP3[1], Settings.GPRSServerIP3[2], Settings.GPRSServerIP3[3]);
	SendSetting_Str(SocketNo, 0x31, sIP);
	//SendSetting_Str(SocketNo, 0x32, GPSCoords);

	// DO NOT USE SETTING 0x2D AS IT IS USED TO RESTART THE RABBIT IP INTERFACE !!
	//SendSetting_Str(SocketNo, 0x2D, sIP);

	tcp_tick(NULL);
}



// ProcessPCCommands
void CheckForPCCommands(char SocketNo)
{
	int iBytesRead, sendviaBT;
   unsigned long i;
   char j;
   char sBuffer[50];

		// Debug Riley
		// Used to get rid of extra character added sometimes bty RFID Server
		int count;
		char temp_buf[200];

   strcpy(sBuffer, "");
   sendviaBT = 0;
   iBytesRead = 0;

   //check also for incoming commands from ethernet
   tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
   iBytesRead = sock_fastread(&MyServerSocket[SocketNo].ServerSocket, sBuffer, sizeof(sBuffer));

   if (iBytesRead == 0 && cRewindPending &&
   	(!Settings.DataOnRequest || (Settings.DataOnRequest && iSendDataToPort)))
   {
   	iBytesRead = 10; // Doesn't really matter what value
      for (j = 0; j <= 30; j++) {sBuffer[j] = sRewindPendingBuf[j];} // String has some null chrs so can't use strcpy
   }
   if (iBytesRead > 0)
   {
      if (sBuffer[0] == 56 || sBuffer[0] == 54) // 8: rewind using date/time, 6: rewind using record numbers
      {
      	if (!Settings.DataOnRequest || (Settings.DataOnRequest && iSendDataToPort))
			{
         	cRewindPending = 0;
	         iRewindSocket = SocketNo;
	         ActionRewindCommand(sBuffer);
         }
         else
         {
            cRewindPending = !cRewindPending; // So you can cancel a pending rewind by sending a second rewind command
            for (j = 0; j <= 30; j++) {sRewindPendingBuf[j] = sBuffer[j];} // String has some null chrs so can't use strcpy
         }
      }
      else if (sBuffer[0] == 57) // 9 Stop rewind
      {
         RWRState = RWR_STOPPED;
      }
      else if (sBuffer[0] == 83 && ProgramState == READING && Settings.System) // S (stop reading)
      {
         UHF_Reader_Control(0);
      }
      else if (sBuffer[0] == 82 && ProgramState == IDLE && Settings.System) // R (start reading)
      {
         UHF_Reader_Control(1);
      }
      else if (sBuffer[0] == 55) // 7 (start sending data from specified time)
      {
         iRewindSocket = SocketNo;
      	StartDataSend(sBuffer);
      }
      else if (sBuffer[0] == 115) // s (stop sending data)
      {
         iSendDataToPort = 0;
      }
      else if (sBuffer[0] == 116) // t (set time)
      {
      	// Riley - this is where time is set (BT hex is 0x74 = 116 (dec))
				// Debug Riley
				/*
					Date/Time string format
					t 11:50:27 08-10-2018

					The below was added 8th October 2018 to
					fix the wrong date being set occasionally due
					to an extra character being added by RFID Server
				*/
				count = 0;
				while (sBuffer[count] != '\0' && count < 21)
				{
					temp_buf[count] = sBuffer[count];
					count++;
				}
				temp_buf[count] = '\0';
				SetDateTimeX(SocketNo, temp_buf);
      }
      else if (sBuffer[0] == 114) // r (get time)
      {
         SendDateTime(SocketNo);
      }
      else if (sBuffer[0] == 0x3F) // ? Get reading status
      {
      	SendReadingStatus(SocketNo);
      }
      else if (sBuffer[0] == 85) // U (get settings)
      {
      	SendSettings(SocketNo);  // will go out the TCP/IP port
      }
      else if (sBuffer[0] == 117) // u (set settings)
      {
         //SetSettings(SocketNo, sBuffer, 0, 0);
      }
      else if (sBuffer[0] == 3) // Config settings from Outreach
      {
			iRewindSocket = SocketNo;
         ProcessRemoteConfigSettings(sBuffer);
      }
      else if (sBuffer[0] == 66) // B = battery level
      {
         //sprintf(sBuffer, "B%d\n",  GetBatLevel());
   		//sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, sBuffer, strlen(sBuffer));
         //tcp_tick(&MyServerSocket[SocketNo].ServerSocket);
      }
      else if (sBuffer[0] == 0x09)
		{
         if(sBuffer[1]==OUTPUT_DEC || sBuffer[1]==OUTPUT_HEX){
         	Settings.OutputType = sBuffer[1];
         	SaveSettings();
         }
		}
   }
}


// Writes status messages to the PC
void WriteStatusMessages()
{
	char buf[100];
	int i;
	struct TRemoteConfigRec RemoteConfigRec;


	if (MS_TIMER - iLastStatusTime > 2000)      //every 2 sec
	{
		//ReadVoltage();
		sprintf(buf, "V=%d\n", batt_percent);
		for (i = 0; i < MAX_SOCKET_CONNECTIONS; i++)
		{
			if (MyServerSocket[i].ClientIsConnected)
			{
				sock_fastwrite(&MyServerSocket[i].ServerSocket, buf, strlen(buf));
				if (iClientType[i] == 1)
				{
					Remote_CreateConfigRecord(&RemoteConfigRec);
					sock_fastwrite(&MyServerSocket[i].ServerSocket, &RemoteConfigRec, sizeof(RemoteConfigRec));
				}
			}
		}
		iLastStatusTime = MS_TIMER;
	}
}


void ProcessDataSocket(char SocketNo)
{
	char buf[30];

	tcp_tick(NULL);
   //if(!tcp_tick(&MyServerSocket[0].ServerSocket)) genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 0);
   if(sock_alive(&MyServerSocket[SocketNo].ServerSocket) == 0)
   {
     	if(iClientType[SocketNo] == 1) iClientType[SocketNo] = 0 ;
      TCPIPOpen(SocketNo);
   }
	if (!sock_established(&MyServerSocket[SocketNo].ServerSocket))// && MyServerSocket.ClientIsConnected == 1)
   {
	 	MyServerSocket[SocketNo].ClientIsConnected = 0;

      //iSendDataToPort = 0; // Stop sending data to the port (if Settings.DataOnRequest == 1)
   }
   else
   {
   	if (MyServerSocket[SocketNo].ClientIsConnected == 0)
      {
      	sprintf(buf, "Connected,%lu,U\n", Settings.iLastTimeSent);
	      sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, strlen(buf));
         sprintf(buf, "V=%d\n", batt_percent);
         sock_fastwrite(&MyServerSocket[SocketNo].ServerSocket, buf, strlen(buf));

	      MyServerSocket[SocketNo].ClientIsConnected = 1;
	      tcp_keepalive(&MyServerSocket[SocketNo].ServerSocket, 0);
         genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 1);
      }
      else
      {
      	CheckForPCCommands(SocketNo);
         WriteStatusMessages(SocketNo);
      }
   }
   //if(!tcp_tick(&MyServerSocket[0])) genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 0);  //does not work!!!
}

void ProcessResetSocket()
{

   auto char buff[38];
   static word port;
   static unsigned long ip;
   auto int len;
   char prefix[8] = {0x01,0x09,0x1E,0x00,0xDC,0x05,0x00,0x00};

   if (sock_established(&ResetSocket.ServerSocket))
   {
   	TCPIPCloseSockets();
      TCPIPOpenSockets();
      TCPIPCloseSocket_FinishLynx();
      TCPIPOpenSocket_FinishLynx();
   }
   len= udp_recvfrom(&UDPSocket.ServerSocket, buff, sizeof(buff),&ip,&port);
   if(len>0){
      memcpy(buff, prefix, 8);
      sprintf(buff+8, "Ultra (Echo)\0");
      sprintf(buff+21,"%02x:%02x:%02x:%02x:%02x:%02x",
       MacAddress[0], MacAddress[1], MacAddress[2], MacAddress[3], MacAddress[4], MacAddress[5]);
      udp_sendto(&UDPSocket.ServerSocket, buff, 38,ip,port);
   }
}

void ProcessDataSockets()
{
	int i;

   for (i = 0; i < MAX_SOCKET_CONNECTIONS; i++)
   {
      ProcessDataSocket(i);
   }
}


void genie_startup()
{
	char date_string[12];
   char status;

 	open_4D();
   status = 0;
   /*
   while(!status){
   	status = genieBegin() ;
   };
   */
   genieBegin(); //remove this line and reinstate lines above after debugging
   genieAttachEventHandler(myGenieEventHandler);

}





void LoadSettings()
{


   doRewind = 0;
	lo_backlight=0;       //added to reduce load on system in UHF reading mode
	diag_visible=0;
	trigger_dim=1;
   admin = 0;  //0 = normal, 1 = admin rights to change txpdr codes and do dfu
	enter_pin = 0;    //state for setting pin for admin rights
	set_time_nrf = 1;
	board_vers = 32;
	next_trigger = 0;
	trigger_time = 0;
   ds_rollover=0;    //1HZ signal from DS3231, = 1 on rollover
	set_time = 0; // 0 if no GPS timeset, 1 if PPS timeset
   time_offset=0;
   Settings.Init = 0;
   SettingsArray[0] = &Settings;
   SettingsLenArray[0] = sizeof(Settings);
   readUserBlockArray(SettingsArray, SettingsLenArray, 1, 0);
   spi_record_state = 1; //will be set to records avail state
   if (Settings.Init != 1)
   {
      Settings.Init = 1;
      Settings.ReaderPower = 80;
   	Settings.Channel = 0;           //0 = channel 1 up to 16
   	Settings.TimeZone = 8; // for testing Perth
   	Settings.Add30 = 0;
      Settings.useDHCP = 0;
      Settings.Beeper = 1;
      Settings.RabbitIP[0] = 192;Settings.RabbitIP[1] = 168;Settings.RabbitIP[2] = 1;Settings.RabbitIP[3] = 90; // 90
      Settings.DataOnRequest = 0;
      Settings.RemoteType = 0;
      Settings.TriggerOn = 0;
      Settings.Brightness = 15;  //maximum
      Settings.Dim = 0;    //off
      Settings.SendDataToRemoteServer = 1;
      strcpy(Settings.APNName, "");
      strcpy(Settings.APNUser, "");
      strcpy(Settings.APNPassword, "");
      Settings.GPRSServerIP1[0] = 0;Settings.GPRSServerIP1[1] = 0;Settings.GPRSServerIP1[2] = 0;Settings.GPRSServerIP1[3] = 0;
      Settings.GPRSServerIP2[0] = 0;Settings.GPRSServerIP2[1] = 0;Settings.GPRSServerIP2[2] = 0;Settings.GPRSServerIP2[3] = 0;
      Settings.GPRSServerIP3[0] = 0;Settings.GPRSServerIP3[1] = 0;Settings.GPRSServerIP3[2] = 0;Settings.GPRSServerIP3[3] = 0;
      Settings.RabbitGateway[0] = 0;Settings.RabbitGateway[1] = 0;Settings.RabbitGateway[2] = 0;Settings.RabbitGateway[3] = 0;
      Settings.RabbitDNS[0] = 0;Settings.RabbitDNS[1] = 0;Settings.RabbitDNS[2] = 0;Settings.RabbitDNS[3] = 0;
      Settings.FirstBoot = 1;
      Settings.UHF_region = 0;
      Settings.System = 0;
      Settings.AutoSetGPSTime = 0;
   }
   if(Settings.FirstBoot)
	 {
		strcpy(Settings.APNPassword, "");
		strcpy(Settings.APNName, "");
		Settings.GPRSServerIP1[0] = 0;Settings.GPRSServerIP1[1] = 0;Settings.GPRSServerIP1[2] = 0;Settings.GPRSServerIP1[3] = 0;
      Settings.GPRSServerIP2[0] = 0;Settings.GPRSServerIP2[1] = 0;Settings.GPRSServerIP2[2] = 0;Settings.GPRSServerIP2[3] = 0;
      Settings.GPRSServerIP3[0] = 0;Settings.GPRSServerIP3[1] = 0;Settings.GPRSServerIP3[2] = 0;Settings.GPRSServerIP3[3] = 0;
      Settings.RabbitGateway[0] = 0;Settings.RabbitGateway[1] = 0;Settings.RabbitGateway[2] = 0;Settings.RabbitGateway[3] = 0;
      Settings.RabbitDNS[0] = 0;Settings.RabbitDNS[1] = 0;Settings.RabbitDNS[2] = 0;Settings.RabbitDNS[3] = 0;
		Settings.FirstBoot = 0;
      if(Settings.AutoSetGPSTime < 0 || Settings.AutoSetGPSTime > 1) Settings.AutoSetGPSTime=0;
	}
   if (Settings.ShutDownStatus != ABNORMAL_SHUTDOWN && Settings.ShutDownStatus != NORMAL_SHUTDOWN) Settings.ShutDownStatus = NORMAL_SHUTDOWN;
   //these are always reset to default values and not saved in userblock
   //Settings.RabbitGateway[0] = 192;Settings.RabbitGateway[1] = 168;Settings.RabbitGateway[2] = 0;Settings.RabbitGateway[3] = 1;
   //Settings.RabbitDNS[0] = 192;Settings.RabbitDNS[1] = 168;Settings.RabbitDNS[2] = 0;Settings.RabbitDNS[3] = 1;
   RFID.playback = 0;
   RFID.bt_adv = 0;
	RFID.chip_sleep = 0;
   chip_reads = 0;
   if(Settings.System != 1) Settings.System = 0 ;
   if (Settings.OutputType < 0 || Settings.OutputType > 1) Settings.OutputType = OUTPUT_DEC;
   SaveSettings();
   GPRS_STATE = GPRS_NOGPRS;    //first time we give modem/lan some time
   Settings.GPRS_CurrentRec = 0;
   if(Settings.RemoteType) Settings.RemoteType = 2; //always start off trying to do remote LAN
   if(Settings.UHF_region>6) Settings.UHF_region = 0;
   if(Settings.System){
   	BitWrPortI ( PBDR, &PBDRShadow, 0, 4);    //PB2 LHi = disabled
      BitWrPortI(PADR, &PADRShadow, 1,0 ); // EN power on Reader PA0
   }else{
      BitWrPortI ( PBDR, &PBDRShadow, 1, 4);
      BitWrPortI(PADR, &PADRShadow, 0,0 ); // EN power on Reader PA0
   }
}


void program_init()
{
   char reg_char;
   firmware_info_t	fi;
   int err;
   unsigned int max_register, set_register;
   float max_value;

   RWRState = RWR_STOPPED;
	iGPSTimeFromRabbit = 0;
   sock_init();



  	//CS1
	BitWrPortI ( PBDR, &PBDRShadow, 1, 0 );      //start hi
  	BitWrPortI ( PBDDR, &PBDDRShadow, 1, 7 );    //PB7 input CS1

	//CS2
	BitWrPortI ( PBDR, &PBDRShadow, 1, 0 );      //start hi
  	BitWrPortI ( PBDDR, &PBDDRShadow, 1, 5 );    //PB5 input CS2

   BitWrPortI (PBDDR, &PBDDRShadow, 1, 6 );    //PB6 output on/off LED green

   //PC0 TXD

   WrPortI ( PCALR, &PCALRShadow, PCALRShadow &~0x03 ); // enable TxD on PC0
   BitWrPortI ( PCDDR, &PCDDRShadow, 1, 0 );    //PC0 output MOSI
   BitWrPortI ( PCFR, &PCFRShadow, 1, 0 );	// PC0 = alternate function

   // DTR2 of modem - set low to activate, high to allow sleep mode
   BitWrPortI ( PEDDR, &PEDDRShadow, 1, 2 );    //PE2 output DTR2
   BitWrPortI ( PEDR, &PEDRShadow, 0, 2);    //PE2 LO   - wakeup

   BitWrPortI ( PEDDR, &PEDDRShadow, 0, 0 );    //PE0 input PPS
   BitWrPortI ( PEDDR, &PEDDRShadow, 0, 1 );    //PE1 input DS3231 pulse
   BitWrPortI ( PEDDR, &PEDDRShadow, 1, 5 );    //PE5 output for fan control

   // set up RxC on PC1
   WrPortI ( SDCR, &SDCRShadow, SDCRShadow &~0x30 );
   BitWrPortI ( PCDDR, &PCDDRShadow, 0, 1 );    //PC1 input MISO

   // set up clock on PE3
  	WrPortI ( PEALR, &PEALRShadow, PEALRShadow | 0xC0 ); // enable Clk on PE3
   BitWrPortI ( PEFR, &PEFRShadow, 1, 3 );	// PE3 = alternate function
   //BitWrPortI ( PEDDR, &PEDDRShadow, 1, 3 );
   BitWrPortI ( PBDDR, &PBDDRShadow, 0, 3 );    //PB3 input from nrf

   BitWrPortI(PEDDR, &PEDDRShadow, 0, 0); // UBlox Lisa: Set PE0 as input
   BitWrPortI(PEDDR, &PEDDRShadow, 0, 1); // nrf52833: Set PE1 as input

    //UHF reader enable pin
   BitWrPortI ( PBDDR, &PBDDRShadow, 1, 4 );    //PB4 as output


   //Set PA2 for output (used to control modem power)
   WrPortI (SPCR, &SPCRShadow, 0x84); //set as output
   BitWrPortI(PADR, &PADRShadow, 0,2 ); // EN power off Modem


   CS1_DISABLE;
   CS2_DISABLE;
   genie_startup();

   genieWriteStr(GENIE_SPLASH_STR, "Setting IP");
   SetRabbitIP();
   genieWriteStr(GENIE_SPLASH_STR, "Initialise SPI");
   SPIinit();
   //SPImode(2);  //for some reason is mode 1 in NRF52833
   SPImode(SPIMODE);
   genieWriteStr(GENIE_SPLASH_STR, "Mounting NAND");
   MountNANDLogFile();
   Settings.GPRS_CurrentRec = GetNANDFileSize()/ sizeof(struct nrf_record);
   iGPRSBaseRecord = Settings.GPRS_CurrentRec;
   iLastLogID =  GetLastLogID();
   screenSleep = 0;

   //set DS3231
   genieWriteStr(GENIE_SPLASH_STR, "Initialise I2C");
   i2c_init();    //initialise first time
   //reg_char = MP_Read(MP2731_CURRENT);
   //reg_char = MP_Read(MP2731_STATUS);
   genieWriteStr(GENIE_SPLASH_STR, "Initialise MP2731");
   //////////////////////


  // check if the 17303 is installed (V3.2 boards)
  max_register = max_read(MAX17303_ADDRESS1, 0x21);
  if(max_register == 0x4067) board_vers=32;
  //max_write(MAX17303_ADDRESS1, MAX17303_FAULTS, 0);
   //////////////////////

   if(board_vers>=32){

   	max_write(MAX17303_ADDRESS2, MAX17303_nPackCfg, 0x101); //do not use external thermister
      /*
      max_register = max_read(MAX17303_ADDRESS1, MAX17303_REPCAP);
   	max_value =  max_register * (float) 0.5; //using 0.01R sense resistor - in mAh
   	max_register = max_read(MAX17303_ADDRESS1, MAX17303_REPSOC);    //capacity %
   	max_value =  max_register * (float) 1/256;  //   in %
   	max_register = max_read(MAX17303_ADDRESS1, MAX17303_TTF);      // Time to full
   	max_value =  max_register * (float) 5.625;  // in seconds
   	max_register = max_read(MAX17303_ADDRESS1, MAX17303_VCELL);
   	max_value =  max_register * (float) 0.078125;  // in volts
   	max_register = max_read(MAX17303_ADDRESS1, MAX17303_CURRENT);
   	max_value =  (signed int) max_register * (float) 0.15625;  // in mA   can be positive or negative
   	max_register = max_read(MAX17303_ADDRESS1, MAX17303_CHARGE_CURRENT);
   	max_value =  max_register * (float) 0.15625 ;  // in mA
      */
      max_write(MAX17303_ADDRESS2, MAX17303_RESISTOR, 0x01F4);     // 0.005 ohm resistor : not used in internal calcs
      max_write(MAX17303_ADDRESS2, MAX17303_JEITAC, 0x64FF);    //set charge current to 4.0A  no temp coef changes
      //max_write(MAX17303_ADDRESS2, MAX17303_JEITAC, 0xD2FF);    //set charge current to 4.2A (default 0x64 = 2A)   4B second byte default
      max_write(MAX17303_ADDRESS2, MAX17303_FOVCP, 0x2208);    //nODSCTh upped the fast overdischarge threshold for modem ops
      //max_write(MAX17303_ADDRESS2, MAX17303_SOVCP, 0x37B5);    //set slow overcharge limit to 4.4A   (default was 4B (first byte) = 3A)
      //SOVCP slow is 2's compliment so is signed. Positive for charge and negative for discharge
      //max_write(MAX17303_ADDRESS2, MAX17303_SOVCP, 0x7F80);    //nIPrtTh1 no slow over discharge current protect as opened fully. 2's complement so be careful calculating!!
      max_write(MAX17303_ADDRESS2, MAX17303_SOVCP, 0x6480);    //nIPrtTh1 no slow over discharge current protect as opened fully. 2's complement so be careful calculating!
      max_register = max_read(MAX17303_ADDRESS2, MAX17303_OVP);

      set_register = 0x70CE;
      if(max_register!=set_register){
   		//Shutdown will be both FET and chip so full depletion = charger input to restart
         //UVP = 2.9V (register value 35), UOCVP = 3.18V (reisyter value 7), UVShdn = 2.58V (register Value -8)   0x8C78
         //UVP = 2.82V (register value 33), UOCVP = 3.18V (reisyter value 9), UVShdn = 2.9V (register Value +2)   0x4092
         //UVP = 2.98V (register value 39), UOCVP = 3.22V (reisyter value 6), UVShdn = 3.02V (register Value +1)   0x9C61
         //UVP = 3.1V (register value 45), UOCVP = 3.3V (reisyter value 5), UVShdn = 3.02V (register Value -2)   0xB452
         //UVP = 3.1V (register value 45), UOCVP = 3.3V, UVShdn = 2.94V (register -4)     0xB454
         //UVP = 3.0V (register value 40), UOCVP = 3.18V, UVShdn = 3.16V (register Value +4)    0xA094
         //UVP = 2.68V (register value 24), UOCVP = 2.96V (value 7), UVShdn = 2.72 (register + 1)    0x6071
         //UVP=2.6V(register 20), UOCVP = 2.92V , UVShdn = 2.44V    0x508C 'as used on demo board
         //UVP=2.6V, UOCVP = 3.16V (14), UVShdn = 3.16V  0x50E1
         //UVP=2.7V (25) , UOCVP = 3.18 (12) , UVShdn = 2.62 (-2) 0x70CE
         max_write(MAX17303_ADDRESS1, MAX17303_OVP_BAK, set_register);
   		max_write(MAX17303_ADDRESS2, MAX17303_OVP, set_register);
      }
      set_register = 0xA03D;
      max_write(MAX17303_ADDRESS2, MAX17303_NVEMPTY, set_register);
      //VEmpty = 3V 0x9661
      //VEmpty = 3.12V 0x9C3D         /
      //VEmpty = 3.2V    0xA03D
      //2.92 0x923D


     MP_Write(MP2731_CHARGE_CURRENT, 0xE0);  //max fast charge of 4.16A only set on bootup as POR
     //MP_Write(MP2731_CHARGE_CURRENT, 0xFF);  //max fast charge of 4.52A only set on bootup as POR
     //MP_Write(MP2731_CHARGE_CURRENT, 0xC3);    //max fast charge of 2.98A only set on bootup as POR
     //MP_Write(MP2731_CHARGE_CURRENT, 0xB8);    //max fast charge of 2.56A only set on bootup as POR
   }else{
   	MP_Write(MP2731_CHARGE_CURRENT, 0xB4);  //max fast charge of 2.4A only set on bootup as POR
   }
   //MP_Write(MP2731_CURRENT, 0x70);      //max current input 2.4A
   MP_Write(MP2731_CURRENT, 0x5C);      //max current input 1.5A         equates to 18W @ 12V
   //MP_Write(MP2731_CURRENT, 0x1C);      //max current input 1.5A         equates to 18W @ 12V       ILIM off
   //MP_Write(MP2731_CURRENT, 0x3F);      //max current input 1.5A         equates to 18W @ 12V       ILIM off
   //MP_Write(MP2731_CURRENT, 0x00);      //disabled ILIM
   //MP_Write(MP2731_CURRENT, 0x5F);      //max current input 1.65A
   MP_Write(MP2731_TIMER, 0x85); //turn watchdog timer off to see if it does not reset charge current after 40s

   MP_Write(MP2731_NTC, 0xDC);  //AICO turned off, others default. Saved on POR
   //MP_Write(MP2731_NTC, 0xCC);  //AICO turned off, NTC battery charge off. Saved on POR
  	MP_Write(MP2731_ADC_CONT, 0xD0);  //AICO turned off, others default. Saved on POR

   MP_Write(MP2731_BATFET, 0x41);    //hardware enabled, on time 0.5sec, off 10 sec
   //MP_Write(MP2731_BATFET, 0x58);    //software enabled, on time 0.5sec, off 8 sec

   //MP_Write(MP2731_CHARGE, 0x58);       //Vsysmin 3.6V   track 150mV
   MP_Write(MP2731_CHARGE_VOLTAGE, 0xA0);    //set to default 4.2V
   MP_Write(MP2731_JEITA, 0xD4);      //VHot = 60 degrees, VWarm = 55 degrees Cool = 5  Cold = 0
   //MP_Write(MP2731_CHARGE, 0x54);          //Vsysmin 3.3V    track 100mV
   MP_Write(MP2731_CHARGE, 0xD4);          //Vsysmin 3.3V    track 100mV   battload enable

   genieWriteStr(GENIE_SPLASH_STR, "Initialise DS3231");
   IntSqw_Set(0x40); //1 HZ
   RTC_Write_Reg(DS3231_REG_STATUS, 0x80);    //need to turn off BB32KHZ in DS3232M chip, bit 6 and 3 to 0
   RTC_Write_Reg(DS3231_REG_CONTROL, 0x00);    //turn off BBSQW but turn on 1HZ SQW
   // Get MAC address
	pd_getaddress(IF_ETH0, MacAddress);


   err = fiProgramInfo(&fi);
   version = fi.version;
   FinishLynxSocket.ClientIsConnected = 0;
   FinishLynx_Last_Time_Sync = 0;



}





cofunc char ConnectToSocketServer()
// Returns 1 if a connection is successfully made otherwise 0.
{
   char buf[100], s[20], c;
   unsigned long i;
	longword iIPAddress;
   int b,delay;
   char response;
   int still_valid_connection;	// Helps to speed up restarting the connection process if one part fails. // Riley
   char valid_signal;


   still_valid_connection = 0;
   response = 0;


   if (Settings.RemoteType == 1)
   {
      //if we have not already initialised modem
      if (GPRS_STATE == GPRS_NOGPRS){
      	//helps negotiate baud rate

         /*
         sprintf(buf,"AT\r");
         serFrdFlush();
   		serFwrite(buf, strlen(buf));
         printf(buf,"\n\r");
      	i = MS_TIMER;
      	while (MS_TIMER - i < 500)  {    //need a delay in here
         	if (GPRS_ReadSer("OK")){
           		// We don't care what response we get, just as long as we get one
            	break;
            }
            yield;
	   	}

         serFrdFlush();
         sprintf(buf,"ATE0\r");
   		serFwrite(buf, strlen(buf));
         printf(buf,"\n\r");
         i = MS_TIMER;
         while (MS_TIMER - i < 500)  {    //need a delay in here
            if (GPRS_ReadSer("OK")) {// We don't care what response we get, just as long as we get one
            	break;
            }
            yield;
	   	}
         serFrdFlush();
         sprintf(buf,"AT&D1\r");
   		serFwrite(buf, strlen(buf));
         printf(buf,"\n\r");
         i = MS_TIMER;
         while (MS_TIMER - i < 500)  {    //need a delay in here
            if (GPRS_ReadSer("OK")) {// We don't care what response we get, just as long as we get one
            	break;
            }
            yield;
	   	}
         */
         //Check signal strength ALWAYS
      	serFwrFlush();
      	serFrdFlush();
      	sprintf(buf, "AT+CSQ\r");
      	serFwrite(buf, strlen(buf));
  	 		printf(buf,"\n\r");
   		i = MS_TIMER;
         valid_signal=0;
			while (MS_TIMER - i < 3000)  {
      		if (GPRS_ReadSer("CSQ:")==1){
               valid_signal = 1;
               break;
         	}
         	yield;
      	}

         //CSQ is OK so proceed
         if(valid_signal){
      	//Set up APN for GPRS connection
         serFwrFlush();
      	serFrdFlush();
      	sprintf(buf, "AT+QICSGP=1,1,\"%s\",\"\",\"\",3\r", Settings.APNName);
   		serFwrite(buf, strlen(buf));
  	 		printf(buf,"\n\r");
   		i = MS_TIMER;
			while (MS_TIMER - i < 1000)  {
				if (GPRS_ReadSer("OK") != 0) {// We don't care what response we get, just as long as we get one
            	break;
            }
            yield;
	   	}
          //Deactivate the GPRS connection - the response can take up to 7 seconds
          /*
      	 sprintf(buf, "AT+QIDEACT=1\r");
          serFwrFlush();
          serFrdFlush();
      	 serFwrite(buf, strlen(buf));
      	 printf(buf,"\n\r");
   		i = MS_TIMER;
			while (MS_TIMER - i < 3000)  {
				if (GPRS_ReadSer("OK") != 0) {// We don't care what response we get, just as long as we get one
					still_valid_connection = 0;
            	break;
         	}
         	yield;
	   	}
         */
         //ALWAYS Activate the GPRS connection - the response can take up to 7 seconds
         serFwrFlush();
      	serFrdFlush();
         sprintf(buf, "AT+QIACT=1\r");
      	serFwrite(buf, strlen(buf));
      	printf(buf,"\n\r");
   		i = MS_TIMER;
			while (MS_TIMER - i < 7000)  {
				if (GPRS_ReadSer("OK") == 1) {// We don't care what response we get, just as long as we get one

            	still_valid_connection = 1;
            	break;
         	}
         	yield;
	   	}
      }


      if(still_valid_connection == 1)
      {
      	still_valid_connection = 0;
	      //create socket
         serFwrFlush();
	      serFrdFlush();
         sprintf(s, "%d.%d.%d.%d", Settings.GPRSServerIP1[0], Settings.GPRSServerIP1[1], Settings.GPRSServerIP1[2], Settings.GPRSServerIP1[3]);
      	sprintf(buf, "AT+QIOPEN=1,0,\"TCP\",\"%s\",%d,0,2\r", s, Settings.GPRSServerPort);
      	serFwrite(buf, strlen(buf));
  	 		printf(buf,"\n\r");
   		i = MS_TIMER;
			while (MS_TIMER - i < 5000)  {
         	if (GPRS_ReadSer("CONNECT") == 1) // We WANT the correct response
	         {
	         	GPRS_STATE = GPRS_CONNECTED;
	            iGPRSLastPulseTime = MS_TIMER;
	            iGPRSTimeofLastResponse = MS_TIMER;
	            GPRS_STATUS = GPRS_STATUS_SOC;
               //modem_reset_counter = 0;
	            return 1;
	         }else{
	         	yield;
	   		}
         }
      }

      printf("No response - back to restart again with GPRS_NOGPRS"   );
      Settings.RemoteType = 2;    //try LAN again next time
      GPRS_STATE = GPRS_NOGPRS;

      sprintf(buf, "AT+QICLOSE=0\r");
      serFrdFlush();
      serFwrite(buf, strlen(buf));
      printf(buf,"\n\r");
   	i = MS_TIMER;
		while (MS_TIMER - i < 5000)  {
			if (GPRS_ReadSer("OK") != 0) {// We don't care what response we get, just as long as we get one
				still_valid_connection = 0;
            break;
         }
         yield;
	   }



      //Deactivate the GPRS connection - the response can take up to 7 seconds

      sprintf(buf, "AT+QIDEACT=1\r");
      serFrdFlush();
      serFwrite(buf, strlen(buf));
      printf(buf,"\n\r");
   	i = MS_TIMER;
		while (MS_TIMER - i < 5000)  {
			if (GPRS_ReadSer("OK") != 0) {// We don't care what response we get, just as long as we get one
				still_valid_connection = 0;
            break;
         }
         yield;
	   }
	   return 0;

    }
    }
    else if (Settings.RemoteType == 2)
    {
      sprintf(s, "%d.%d.%d.%d", Settings.GPRSServerIP1[0], Settings.GPRSServerIP1[1], Settings.GPRSServerIP1[2], Settings.GPRSServerIP1[3]);
      //LANServer is not used in EchoActive - same as GPRSServer
      //if (Settings.LANServer[0] != NULL)
      if (s != NULL)
      {
	      iIPAddress = resolve(s);
	      if (iIPAddress == 0){
	         printf("Cannot resolve IP address or URL!\n");
            genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_REMOTE, 0);
            Settings.RemoteType = 0;    //wrong IP address value, turn off remote
	      }else{
	         printf("IP address resolved\n");
         }
			i = MS_TIMER;
	      //sock_abort(&LANClientSocket);
	      sock_close(&LANClientSocket);
         tcp_tick(NULL);
	      if (iIPAddress > 0)
	      {

         	if(!tcp_open(&LANClientSocket, 0, iIPAddress, Settings.GPRSServerPort, NULL))
            {
            	printf("TCP Open failure");
            }
	         else while(!sock_established(&LANClientSocket))
	         {
	            yield;
	            if (!tcp_tick(&LANClientSocket) || MS_TIMER - i >= LANSERVER_CONN_TIMEOUT)
	            {
	               break;
	            }
	         }
	         if (sock_established(&LANClientSocket))
	         {
	            printf("Connected to remote socket using LAN!\n");
	            GPRS_STATE = GPRS_CONNECTED;
	            iGPRSLastPulseTime = MS_TIMER;
	            iGPRSTimeofLastResponse = MS_TIMER;
	            GPRS_STATUS = GPRS_STATUS_SOC;
	            return 1;
	         }
	         else
	         {
               Settings.RemoteType = 1;    //fallback to 4G modem
               Toggle_Modem();   //wakeup modem
               printf("Cannot connect to remote socket via LAN trying modem!\n");
	            return 0;
	         }
	      }
	      else return 0;
      }
      else return 0;
   }
}



void Remote_Main()
{
   unsigned long i, j;

   //toggle the Led on and off if modem is used and socket connected
   if (GPRS_STATE == GPRS_CONNECTED)
   {
      //4G modem and connected
      if(Settings.RemoteType) genieWriteObject(GENIE_OBJ_LED, GENIE_LED_GSOC, 1);
      else genieWriteObject(GENIE_OBJ_LED, GENIE_LED_GSOC, 0);
   }
   else
   {
   	//no 4G socket connection
      genieWriteObject(GENIE_OBJ_LED, GENIE_LED_GSOC, 0);
      //genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 0);
   }
   //if (GPRS_STATE == GPRS_NOGPRS || GPRS_STATE == GPRS_RECONNECT)

   if (GPRS_STATE == GPRS_NOGPRS)
   {
   	// Note: if we are using external router, then no connection needed here
      // as there should already be one. All we need to do is connect to the socket server

      GPRS_STATUS = GPRS_STATUS_CON;
      costate
      {
      	wfd ConnectToSocketServer();
        abort;
      }
   }
   if (GPRS_STATE == GPRS_DISCONNECTED){     //Allow 1 cycle before going to reconnection attempt
   	Toggle_Modem();
   	GPRS_STATE = GPRS_NOGPRS;
   }
   else if (GPRS_STATE == GPRS_CONNECTED)
   {

   	if (Settings.SendDataToRemoteServer == 1)
	   {
	      OpenNANDLogFile(0);
         Remote_SendNextBatch();
	      CloseNANDLogFile();
	   }
      Remote_CheckForResponse();
      Remote_SendConfigRecord();

   }
}

void Remote_Process()
{
   if (Settings.RemoteType)
   {

      if (MS_TIMER - iGPRSLastProcessTime > gprs_wait_time)
	   {
	   	if (GPRS_STATE == GPRS_NOGPRS)
	      {
            //gprs_wait_time = 1000;   //change to 1 sec
            GPRS_STATUS = GPRS_STATUS_CON;
	      }
			Remote_Main();
			iGPRSLastProcessTime = MS_TIMER;
	   }
	}else{
   	genieWriteObject(GENIE_OBJ_LED, GENIE_LED_GSOC, 0);
      //genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 0);
   }
}







#ifdef BU_TEMP_USE_FAT
void unmount_all()
{
	int i, rc;

   for (i = 0; i < num_fat_devices * FAT_MAX_PARTITIONS;
   	i += FAT_MAX_PARTITIONS)
   {
      if (fat_part_mounted[i])
      {
      	do {
	         rc = fat_UnmountDevice(fat_part_mounted[i]->dev);
	      } while (rc == -EBUSY);
         if (rc < 0)
         {
            printf("Unmount Error on %c: %ls\n", 'A' + i, strerror(rc));
         }
      }
   }
}
#endif

/*
	Return Codes:
		Note that this function will either succeed and reboot to new firmware,
		or it will fail with one of the following error codes:

	      -EILSEQ: Not a valid firmware_info_t struct (bad marker bytes
	               or unsupported version of structure).
	      -EBADMSG: Bad CRC (structure has been corrupted).
	      -ENODATA: Source not open, or firmware info not found in source.
	      -EPERM: Firmware was compiled for a different target.
	      -EBADDATA: CRC-32 mismatch, firmware image corrupted.
	      -EBADMSG: CRC-32 mismatch after installing.
	      -ENOMEM: Couldn't allocate buffer to copy firmware.
	      -ENODATA: Download didn't contain a valid firmware image for this
	      			device.

	      Error codes when using a FAT file for temporary storage:
	      -EINVAL: Couldn't parse BU_TEMP_FILE.
	      -ENOENT: File BU_TEMP_FILE does not exist.
	      -EMFILE: Too many open files.

	      Error codes when using the serial flash for temporary storage:
	      -ENODEV: Can't find/read the serial flash.
*/


int install_firmware()
{
	firmware_info_t fi;
   bu_download_t	dl;
	int			i;
	int 			result;
	int			progress;
   char 			message[20];

   printf( "verifying and installing new firmware\n");


   #ifdef BU_TEMP_USE_FAT
	// Auto-mount the FAT filesystem
	printf( "mounting FAT partitions\n");
   do {
	   result = fat_AutoMount(FDDF_USE_DEFAULT);
   } while (result == -EBUSY);
   if (result == -EIO || result == -ENOMEDIUM)
   {
		printf( "Fatal device initialization error!  Exiting now.\n");
		return result;
   }
   if (! fat_part_mounted[0])
   {
		printf( "Couldn't mount A:\n");
		return -1;
   }

	// register a function to unmount all FAT volumes on exit
   atexit( unmount_all);
	#endif

   result = buDownloadInit( &dl, &download_sock, FIRMWARE_URL);
   if (result)
   {
      printf( "couldn't initiate download (error %d)\n", result);
      return -1;
   }
   else
   {
      while ((result = buDownloadTick( &dl)) == -EBUSY)
      {
         if (dl.filesize)
         {
            printf( " %lu/%lu (%u%%)\r", dl.bytesread, dl.filesize,
               (int) (dl.bytesread * 100 / dl.filesize));
            sprintf(message, " %lu/%lu (%u%%)\r", dl.bytesread, dl.filesize,
               (int) (dl.bytesread * 100 / dl.filesize));
         }
         else
         {
            printf( " %lu bytes read\r", dl.bytesread);
            sprintf(message, "%lu bytes read\r", dl.bytesread);
         }
         genieWriteStr(GENIE_FW_PROGRESS_STR, message);
      }

      if (result == -ENODATA)
      {
         printf( "download canceled, file did not contain valid firmware\n");
         return result;
      }
      else if (result == 0)
      {
         printf( "download complete, %lu bytes\n", dl.bytesread);
         genieWriteStr(GENIE_FW_PROGRESS_STR, "Download Complete");
      }
      else
      {
         printf( "download canceled (error %d)\n", result);
	      #ifdef BU_TEMP_USE_DIRECT_WRITE
	         printf( "attempting to restore boot firmware from RAM\n");
	         // There was an error downloading or installing the firmware,
	         // so we need to restore the boot firmware image from the copy
	         // of the program running in RAM.
	         result = buRestoreFirmware( 0);
	         if (result)
	         {
	            printf( "error %d attempting to restore firmware\n", result);
	            // At this point, the firmware stored on the boot flash is
	            // corrupted and cannot be restored.
	         }
	         else
	         {
	            printf( "restore complete\n");
	         }
	      #endif
      }
   }

   result = buOpenFirmwareTemp( BU_FLAG_NONE);
   if (!result)
   {
	   // buGetInfo is a non-blocking call, and may take multiple attempts
	   // before the file is completely open.
	   i = 0;
	   do {
	      result = buGetInfo( &fi);
	   } while ( (result == -EBUSY) && (++i < 20) );
   }

   if (result)
   {
      printf( "Error %d reading new firmware\n", result);
   }
   else
   {
      printf( "Found %s v%u.%02x...\n", fi.program_name,
         fi.version >> 8, fi.version & 0xFF);

      printf( "Attempting to install new version...\n");
      progress = 0;
      do
      {
         printf( "\r verify %u.%02u%%\r", progress / 100, progress % 100);
         result = buVerifyFirmware( &progress);
      } while (result == -EAGAIN);
      if (result)
      {
         printf( "\nError %d verifying firmware\n", result);
         printf( "firmware image bad, installation aborted\n");
         genieWriteStr(GENIE_FW_PROGRESS_STR, "Error");
      }
      else
      {
         printf( "verify complete, installing new firmware\n");
         genieWriteStr(GENIE_FW_PROGRESS_STR, "Installing");
         result = buInstallFirmware();
         if (result)
         {
            printf( "!!! Error %d installing firmware !!!\n", result);
            genieWriteStr(GENIE_FW_PROGRESS_STR, "Failed");
         }
         else
         {
            printf( "Install successful: rebooting.\n");
            genieWriteStr(GENIE_FW_PROGRESS_STR, "Rebooting");
            exit( 0);
         }
      }
   }

   // make sure firmware file is closed if there were any errors
	while (buCloseFirmware() == -EBUSY);

   return result;
}



int check_fw()
{
	int 			result;
   int retval;
   char body[65];
   char *s;
   char check_url[35];
   int n;
   char fw_str[6];



   //added code to check if update is there
    genieWriteStr(GENIE_FW_PROGRESS_STR, "Checking for Update...");
    //retval = httpc_init (&hsock, &check_sock);
    //if(retval) return -1;
    //retval = httpc_get_url (&hsock, CHECK_URL);
    retval = httpc_get (&hsock, "www.rfidtiming.com", 80, "/Echo_VPlus.htm", NULL);
    if(retval) return -1;
    while (hsock.state == HTTPC_STATE_HEADER)
    {
     	retval = httpc_read_header (&hsock, CHECK_URL, sizeof(CHECK_URL));
    }
    while (hsock.state == HTTPC_STATE_BODY)
    {
    	retval = httpc_read_body (&hsock, body, 65);
      if(retval){
      	printf("%s", body);
      }
   }
   s = body;
   n = 0;

   httpc_close (&hsock);
   tcp_tick(NULL);
   do {
   	if (!memcmp(s, "<h1>", 4))
	   {
         s=s+4;
         memcpy(fw_str, s, 6);
         new_fw_vers = strtol(fw_str, NULL, 0);
         if(_FIRMWARE_VERSION_ ==  new_fw_vers) return 0; //exit function as FW up to date
         return 1;;

      }
      s++;
      n++;
      if(n > 30) return -1; //exit as could not find FW version
	} while(1);
}



void trigger_do (void)
{

	struct nrf_record ptr_record;
   char trig[6] = "TRIGGR";

    CurTime.tm_hour  = mytime->hours;
	CurTime.tm_min  =  mytime->minutes;
	CurTime.tm_year =  mytime->year + 100;
	CurTime.tm_mon = mytime->month;
	CurTime.tm_mday = mytime->day;
	CurTime.tm_wday = mytime->dow - 1;
	CurTime.tm_sec = mytime->seconds;
	tm_wr(&CurTime);
   ptr_record.date_time = mktime(&CurTime);
    sprintf(ptr_record.xpdr_code, "%.6s", trig);
    //calculate ms
    ptr_record.ms = (MS_TIMER - iDSTimeFromRabbit) & 0xFFFFFFFF;
    Beep();
    //get time
    iLastLogID++;
    ptr_record.LogID = iLastLogID;
    trigger_time = 1;
    WriteChipToSocket(ptr_record, 0, 0, 0);
    OpenNANDLogFile(1);
    ptr_record.max_RSSI=0;
    ptr_record.wake_count=0;
    ptr_record.battery=0;
    ptr_record.loop_data=0;
    SaveRAMToNAND(&ptr_record, 1);
    CloseNANDLogFile();


}

//ISR for clock seconjds tickover and trigger if enabled

nodebug root interrupt void DS3231_isr()
{

   /*if(BitRdPortI(PEDR, 5) && Settings.TriggerOn){
   	//need to add a debounce routine so trigger will only fire after 3 seconds wait
   	if((long)MS_TIMER - next_trigger > 0){
         trigger_do();
      	next_trigger = MS_TIMER + TRIGGER_DELAY;
      }
   }else{
   */
   	iDSTimeFromRabbit = MS_TIMER;
   	ds_rollover = 1;
   //}
}



//Main Loop


main()
{
	char charge_type, nrf_status;
   char date_string[12];
   char version_str[7];
   char randomstr[20];
   char toggle;
   float curr_batt_voltage;
   char reg_char, prev_char, error_char;
   //char ubx_tp[8] = {0xB5,0x62,0x0D,0x01,0x00,0x00,0x00,0x00};
   //char ubx_mon_hw[8] =  {0xB5,0x62,0x0A,0x09,0x00, 0x00, 0x00, 0x00};
   signed int max_register;
   float max_value;
   char messge[8];
   int current, j;
   unsigned long tm_now;
   char fan_toggle;
   int fan_temp_thresh;
   int battery_temp_thresh; //percent = 45 degrees
   float temp_percent;


   //variables that were wrongly set in global declarations
   //declaring values globally puts data in flash in DC!!!
   toggle = 0;
   reg_char = 0;
   prev_char = 0;
   error_char = 0;
   fan_toggle=0;
   fan_temp_thresh = 44;
   battery_temp_thresh = 43;
   iLastProcessChip = 0;
	iLastArrayProcessTime = 0;
	uhf_reading = 0; //read or not for UHF reader
	uhf_duty_cycle = 0; //the on off state of duty cycling
	uhf_mode = 1; //default finish line
	endAntRound = 0;
	readerTemp = 0;
   encode_tag = 0;
   //transmitter_off = 0;
   ///////////////////

   //Buzzer
   BitWrPortI (PBDDR, &PBDDRShadow, 1, 2 );    //PB2 output Buzzer
   BitWrPortI (PBDR, &PBDRShadow, 1, 2 );    //Buzzer On
   BitWrPortI (PBDR, &PBDRShadow, 1, 6 ); //green
   msDelay(2000);    // trying to fix boot up lock
   LoadSettings();    //moved here for quick assess
   program_init();
   BitWrPortI (PBDR, &PBDRShadow, 0, 2 );    //Buzzer Off
   //msDelay(500);   //let splashscreen stay on for another 0.5 sec


   pps = 0;
   old_pps = pps;


   BitWrPortI ( PEDR, &PEDRShadow, 1, 5 );      //fan on
   // seem to have to do comms with GPS chip on SPI for proper SPI comms to nrf chip
   	CS2_ENABLE;
      msDelay(1);// on SPI there needs to be at least 10us between cs going lo and stream of bytes
   	SPIRead(&randomstr, 20);
   	CS2_DISABLE;
   msDelay(200);
   comms_NRF(0x0E);    //get FW version on nrf52833
   sprintf(version_str, "RCM %02d.%02d nRF%d E+",  _FIRMWARE_VERSION_ >> 8, _FIRMWARE_VERSION_ & 0xFF, nrf52833_FW);
   genieWriteStr(GENIE_SPLASH_STR, version_str);
   genieWriteContrast(Settings.Brightness);
   msDelay(2500);

   BitWrPortI ( PEDR, &PEDRShadow, 0, 5 );      //fan off



   SetVectExtern(1, DS3231_isr);
   if(Settings.TriggerOn){
   	WrPortI(I1CR, &I1CRShadow, 0x25);   // enable external INT1 on PE5 and PE1, falling edge PE1, rising edge PE5 (remember inverted), priority 1
   }else{
   	WrPortI(I1CR, &I1CRShadow, 0x05);    //PE1 is 1 HZ timepulse set for falling edge = when second rollsover
   }



   serCrdFlush();
   serCwrFlush();
   genieWriteStr(GENIE_SPLASH_STR, "Setting IP");
   TCPIPOpenSockets();


   iGPRSLastProcessTime = MS_TIMER; //wait 5 seconds before trying remote procedures
   gprs_wait_time = 1000;
   httpc_init (&hsock, &check_sock);
   genieWriteStr(GENIE_SPLASH_STR, "Set GPS");
   set_UBX(); //updates Timepulse parameters on NeoM8J
   if(!Settings.RemoteType){
   	genieWriteStr(GENIE_SPLASH_STR, "Set Modem Off");
    	Toggle_Modem();   //turn off modem for power saving only if remote off and coming from bootup
   }
   SetVectExtern(0, my_isr);        //PPS signal interrupt enabled
   WrPortI(I0CR, &I0CRShadow, 0x09);		// enable external INT0 on PE0, rising edge, priority 1

   iGPSTick = 0;
   //comms_NRF(0x0D);
   checkInterval = MS_TIMER + 2000; //get battery status
   checkInterval2 = MS_TIMER + 15000;     //update sats after 15 sec
   genieActivateForm(GENIE_FORM_MAIN);
   updateGenie_Main();
   if(Settings.System){
    	 ProgramState = IDLE;
    	 comms_NRF(0x0C);    //turn LF transmitter off if using UHF reader
   }else{
   	comms_NRF(0x0A);    //set the channel on the nrf52833
      comms_NRF(0x03);    //set the LF power on the nrf52833
   }
    mytime = RTC_Get();

    CurTime.tm_hour  = mytime->hours;
	 CurTime.tm_min  =  mytime->minutes;
	 CurTime.tm_year =  mytime->year+100;
	 CurTime.tm_mon = mytime->month;
	 CurTime.tm_mday = mytime->day;
	 CurTime.tm_wday = mytime->dow-1;
	 CurTime.tm_sec = mytime->seconds;

    //DS is 2000, GPS is 1900
    tm_wr(&CurTime);  //updates rabbit RTC

    //////


    tm_now = read_rtc();

    LastTouchTime = SEC_TIMER;


   TCPIPOpenSocket_FinishLynx();

   if(Settings.ShutDownStatus == ABNORMAL_SHUTDOWN) UHF_Reader_Control(1);   //start the readers straight away

   for(;;)
   {
      //tcp_tick(NULL);		// Drive DHCP and everything else.
      ProcessDataSockets(); // Check for commands from PC
      ProcessResetSocket(); // Check reset port to see if a client has connected
      genieDoEvents();
      if(MS_TIMER - BEEPDELAY > LastBeepTime ) BitWrPortI(PBDR, &PBDRShadow, 0, 2);
      if(Settings.Dim){
      	if(LastTouchTime + DIMDELAY < SEC_TIMER && trigger_dim){
         trigger_dim = 0;     //reset
         genieActivateForm(GENIE_FORM_SLEEP);
         genieWriteContrast(0);
      	}
      }
      //////////////
      //notes on timing of DS3231
      //There is a square wave of 500ms on and 500ms off
      //The time registers update soon after the falling edge
      //to get the correct time we should read the RTC of DS3231 soon after the falling edge
      //******the nrf chip should update the second counter on the falling edge*******

      if(pps!=old_pps)
      {
         	// we have PPS from Ublox. Set times
         	printf("Time diff = %d \n", time_diff_pps);

            if(!set_time && Settings.AutoSetGPSTime)
            {
            	//set DS3231 from GPS only on first time only. When out of sync this is done within ISR's
               SetTime_uBlox();
               if(set_time){
               	mytime = RTC_Get();
               	genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_MIN, mytime->minutes);
         			genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_HOUR, mytime->hours);
         			sprintf(date_string, "%02u-%02u-20%02u", mytime->day, mytime->month, mytime->year);
   					genieWriteStr(GENIE_DATE_STR,  date_string);
                  if(!BitRdPortI(PBDR, 3)){
         				comms_NRF(0x08);  //update time on nrf52833
         			}

            	}




            }
      		genieWriteObject(GENIE_OBJ_LED, GENIE_LED_PPS, pps);
            old_pps = pps;
      }

      if(ds_rollover==1)
      {
      	//if(!BitRdPortI(PBDR, 3)){   //highest priority to receiving records from nrf before changing time.
         //1 second update. Comes half way through second change so seconds is correct for 500ms
         //comms_NRF(0x08);  //update time on nrf52833
         if(ProgramState == READING){       //indicate on button led we are in read mode
         	BitWrPortI ( PBDR, &PBDRShadow, toggle, 6 );      //start lo
            toggle ^= 1;
            //BitWrPortI ( PADR, &PADRShadow, toggle, 0 );      //start hi
         }
         //new code
         mytime = RTC_Get();
         ds_rollover = 2;   //reset for next second
         if(mytime->seconds==0)
         {
         	genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_MIN, mytime->minutes);
            if(mytime->minutes==0)
            {
					genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_HOUR, mytime->hours);
					if(mytime->hours==0)
               {
               	sprintf(date_string, "%02u-%02u-20%02u", mytime->day, mytime->month, mytime->year);
   					genieWriteStr(GENIE_DATE_STR,  date_string);
               }
            }
            iDSTimeFromRabbit = MS_TIMER; //update the ms timer
         }
         genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_SEC, mytime->seconds);
         //send the time to nrf chip on startup and any change in time
         if(set_time_nrf){
   			if(!BitRdPortI(PBDR, 3)){
   				comms_NRF(0x08);  //update time on nrf52833
         		set_time_nrf = 0;
   			}
			}

         //end new code

         reg_char = MP_Read(MP2731_TIMER);
   		MP_Write(MP2731_TIMER, reg_char | 0x08);     //reset watchdog
         /*
         reg_char = MP_Read(MP2731_FAULT);
         reg_char = MP_Read(MP2731_STATUS);
         if(reg_char & 0xE0){
         	//USB connected                                                                                                                                            1
         }/*else if(OTG_stat){
         	//Start OTG for continuation of ADC conversion
         	OTG_stat = 0;
           MP_Write(MP2731_CHARGE, 0x7B);     //start OTG
         }

         reg_char = MP_Read(MP2731_CURRENT);
         reg_char = MP_Read(MP2731_BATFET);
         reg_char = MP_Read(MP2731_NTC);
         reg_char = MP_Read(MP2731_CHARGE);
         */
         //check to see if charging
      //max_register = max_read(MAX17303_ADDRESS1, MAX17303_CURRENT);
   	  //	max_value =  (signed int) max_register * (float) 0.15625;  // in mA   can be positive or negative
         reg_char = MP_Read(MP2731_STATUS);
         //sprintf(messge, "%x\n\r", reg_char);
         //genieWriteStr(GENIE_DATE_STR,  messge);
         if(reg_char != prev_char){
            prev_char = reg_char;
         	charge_type = reg_char & 0xE0; //only top 3 bits interested
            if(board_vers<32){
         		if(charge_type==0x20){      //non standard adapter limit to 2.4A
            		MP_Write(MP2731_CURRENT, 0x5C);      //max current input 1500mA
         		}else if(charge_type > 0x20 && charge_type < 0xA0){    //SDP/CDP/DCP limit to 500mA
            		MP_Write(MP2731_CURRENT, 0x48);      //max current input 500mA
         		}else if(charge_type == 0xA0){      //Fast Charge......3A+
            		//do not want to push it so keep at 1.5A
               	MP_Write(MP2731_CURRENT, 0x5C);      //max current input 1500mA
         		}else{      //no connection or OTG so revert 500mA
               	MP_Write(MP2731_CURRENT, 0x48);      //max current input 500mA
         		}
            }
            reg_char &= 0x18;
         	if(reg_char) {
            		genieWriteObject(GENIE_OBJ_USERIMAGES, 0x01, 1); //Show image 0
         	}else{
             		genieWriteObject(GENIE_OBJ_USERIMAGES, 0x01, 0); //Show image 1
            }
         }
         /*
         //MP2731 battery current charge
               reg_char = MP_Read(MP2731_ADC_CURRENT);
               sprintf(messge, "%x\n\r", reg_char);
               genieWriteStr(GENIE_DATE_STR,  messge);
         //check faults

        	reg_char = MP_Read(MP2731_FAULT);
         sprintf(randomstr, "%02XFault\n", reg_char);
      	genieWriteStr(GENIE_DATE_STR, randomstr);
         reg_char = 0;
         */

      }
      /////////////////////////////////// UHF READER


      if(uhf_reading){
         //First check to see if we should turn off/on reader for duty cycle
         /*
         if(uhf_duty_cycle){
         	TM_ReadSerialPort(3);
         	//if(endAntRound || (MS_TIMER - iTimeOut > 0)){    //usually get a endAntRound but might miss it so use a timeout as backup
            if(endAntRound || MS_TIMER - iTimeOut > 300){
               StopReaders();
            	endAntRound=0;
            	TM_ProcessChipArray();
            	iNextReadCycleTime = MS_TIMER + duty_cycle;
            	uhf_duty_cycle = 0;
            }
         }else{

         	//if((long)MS_TIMER - iNextReadCycleTime > 0){       //this is if we use a duty cycle only
            	StartReaders();
               uhf_duty_cycle = 1;
               iTimeOut = MS_TIMER;   //we set this as fallback in case endAntRound is not seen
         	//}
         }
         */
         // 100% duty cycle code
         TM_ReadSerialPort(3);
         TM_ProcessChipArray();

      }

      /////////////////////////////////
      if(RWRState!=RWR_READING) Remote_Process();

      if((long)MS_TIMER - checkInterval > 0){
         if(!BitRdPortI(PBDR, 3)){
            if(board_vers>=32){
               max_register = max_read(MAX17303_ADDRESS1, MAX17303_REPSOC);    //capacity %
   				max_value =  max_register * (float) 1/256;  //   in %
               max_value += 1;         //never seems to get to 100 so top it up 1
               if(max_value>100) max_value = 100;
               //printf("Batt Percent = %f\n\r", max_value);
               batt_percent = (int) max_value;;
               genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_BAT, batt_percent);
            	genieWriteObject(GENIE_OBJ_LED_DIGITS, GENIE_DLED_BAT, batt_percent);

               //latches error

               /*
               	error_char = MP_Read(MP2731_FAULT);
               	sprintf(messge, "%x\n\r", error_char);
               	genieWriteStr(GENIE_DATE_STR,  messge);


               max_register = max_read(MAX17303_ADDRESS1, MAX17303_AV_CURRENT);
   				max_value =   max_register * 0.3125;  // in mA   can be positive or negative  //using 0.05mohm current sense resistor
               sprintf(messge, "%f",  max_value);



               max_register = max_read(MAX17303_ADDRESS1, MAX17303_MAXMIN_CURR);
               max_register = (max_register>> 8) & 0xFF;
               max_value = max_register * 0.3125;
               printf("Max Current register is = %d and in = %f mA\n\r", max_register, max_value);
               max_write(MAX17303_ADDRESS1, MAX17303_MAXMIN_CURR, 0x807F);    //resets

               */
               //genieWriteStr(GENIE_TXPDR_STR,  messge);
               max_register = max_read(MAX17303_ADDRESS1, MAX17303_CURRENT);
               max_value = max_register * 0.3125;     // current in mAmps
               printf("Current is = %f mA\n\r", max_value);

   				curr_batt_voltage =  max_read(MAX17303_ADDRESS1, MAX17303_VCELL) * 0.078125;  // in volts
               //printf("Batt Voltage is = %f mV\n\r", curr_batt_voltage);


               max_register = max_read(MAX17303_ADDRESS1, MAX17303_AV_CURRENT);
               //2's complement

               max_value = max_register * 0.3125;     // current in mAmps
               printf("%.1fmA %.1fV %.1fW\n\r", max_value, curr_batt_voltage, max_value/1000*curr_batt_voltage/1000 );
               //sprintf(messge, "%.1f %.1f %.1f\n\r", max_value/1000, curr_batt_voltage/1000, max_value/1000*curr_batt_voltage/1000);
               //genieWriteStr(GENIE_DATE_STR,  messge);



               //reg_char = MP_Read(MP2731_TEMP);
               //temp_percent =  (float) reg_char/255*100;
               //printf("Batt NTC Temp is = %f percent \n\r", temp_percent);

               //battery status on network connection
               /*
               max_register = max_read(MAX17303_ADDRESS1,  MAX17303_FAULTS);
               sprintf(messge, "%x\n\r", max_register);
               genieWriteStr(GENIE_DATE_STR,  messge);
               */

               sprintf(randomstr, "V=%d\n", batt_percent);
               for (j = 0; j < MAX_SOCKET_CONNECTIONS; j++)
   				{
         			if (MyServerSocket[j].ClientIsConnected)
         			{
                     sock_fastwrite(&MyServerSocket[j].ServerSocket, randomstr, strlen(randomstr));
	         			tcp_tick(NULL);
         			}
      			}


               //dim backlight on low battery to avoid overloading the system voltage when UHF reader transmitting
               if(ProgramState == READING){
               	if(!lo_backlight && batt_percent < 50){
               		genieWriteContrast(3);
                  	lo_backlight = 1;
               	}else{
               		if(lo_backlight && batt_percent >= 50){
                  		genieWriteContrast(Settings.Brightness);
                 			lo_backlight = 0;
                  	}
                  }
               }
            }else{
            	//comms_NRF(0x0D);   //update battery status
            }
      		checkInterval = MS_TIMER + DELAY;
            //GetGPS_Signal();
         }
      }
      if(set_time){
      	if((long)MS_TIMER - checkInterval2 > 0){
         	if(!BitRdPortI(PBDR, 3)){
            	GetGPS_Signal();
      			checkInterval2 = MS_TIMER + 30000;   //update sats every 30 sec
         	}
         }
      }

   	if(BitRdPortI(PBDR, 3)){
      	comms_NRF(spi_record_state);
      }
      if(encode_tag){
      	comms_NRF(0x05);
      }
      /*
      if(transmitter_off){
      	comms_NRF(0x0C);
         transmitter_off = 0;
      }
      */
      if(trigger_time){
      	trigger_time = 0;
         genieWriteStr(GENIE_TXPDR_STR,  "TRIG");
         genieWriteStr(GENIE_TXPDR_BAT_STR, "");
      }
      costate
      {
      	wfd RewindLogFile_BinarySearch(8);
        	abort;
      }

      //Turn on over a temp, stay on until temp drops by 5 degrees
      /*
      if(readerTemp >= fan_temp_thresh  || ((temp_percent < battery_temp_thresh) && charging)){
      	if(!fan_toggle){
      		fan_toggle = 1;
      		fan_temp_thresh = 40;
         	BitWrPortI ( PEDR, &PEDRShadow, 1, 5 );      //fan on
         }
   	}else{
      	if(fan_toggle){
           	BitWrPortI ( PEDR, &PEDRShadow, 0, 5 );      //fan off
           	fan_temp_thresh = 44;
           	fan_toggle = 0;
         }
      }

      error_char = MP_Read(MP2731_FAULT);
   	if(error_char!=0x00 && error_char!= 0x20){
      	sprintf(messge, "%x\n\r", error_char);
         genieWriteStr(GENIE_DATE_STR,  messge);
         BitWrPortI(PBDR, &PBDRShadow, 1, 2);
         for(;;){};
      }
      */
   }
}