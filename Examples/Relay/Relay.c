#include "OrionPublicPacket.h"
#include "earthposition.h"
#include "OrionComm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>

// A structure representing a UDP socket destination
typedef struct {
	int socket_desc;
	struct sockaddr_in destination;
    int port;
} udpsender;

// A structure representing a group of socket destinations
#define MAX_SENDERS 3
typedef struct {
    int destinationCount;
    udpsender sender[MAX_SENDERS];
} sendergroup;

// Create the senders we need (Raw, Status, and Track)
sendergroup rawSenders;
sendergroup statusSenders;
sendergroup trackSenders;

//Used to create buffers that manage the JSON message
#define THING_MAX_FIELDS 20
#define THING_FIELD_INDEX 0
#define THING_FIELD_LENGTH 1
typedef struct {
    char *filename;
    FILE* ptr;
    long int size;
    char *buffer;
    int fields[THING_MAX_FIELDS][2];
} filething;

//Used to manage Track state
typedef enum {
    TRACK_NONE = 0x00,   //Not tracking
    TRACK_NEW = 0x01,    //First tracking packet
    TRACK_TRACK = 0x02,  //Ongoing tracking packet
    TRACK_BREAK = 0x03   //First not tracking packet after tracking.
} TrackState;

typedef struct{
    int trackId;
    TrackState trackState;
} TrackStatus;

// Incoming and outgoing packet structures. Incoming structure *MUST* be persistent
//  between calls to ProcessData.
static OrionPkt_t PktIn, PktOut;

// A few helper functions, etc.
long int getFileSize(FILE* fp);
void thingInit(filething *thing,char filename[]);
void populateFieldData(filething *thing);
void populateFileBuffer(filething *thing);
void writeToBufferField(filething *thing,int fieldnum,char string[]);
int udpsenderInit(udpsender *sender,char dest_ip[],int dest_port);
int udpsenderSend(udpsender *sender,char buffer[],int buff_len);
static void getAddr(char addr[],char *arg);
static int getPort(char *arg);
static void KillProcess(const char *pMessage, int Value);
static void ProcessArgs(int argc, char **argv);
static BOOL ProcessData(void);
// static BOOL ProcessData(filething *StatusJson,filething *TrackJson);
static void help();

filething StatusJson;
filething TrackJson;

TrackStatus trackStatus;

int main(int argc, char **argv)
{
    int WaitCount = 0;

    
    // Process the command line arguments
    ProcessArgs(argc, argv);

    printf("Raw Senders: %d\n",rawSenders.destinationCount);
    printf("Status Senders: %d\n",statusSenders.destinationCount);
    printf("Track Senders: %d\n",trackSenders.destinationCount);

    thingInit(&StatusJson,"Status.json");
    thingInit(&TrackJson,"Track.json");

    // Opening file in reading mode
    StatusJson.ptr = fopen(StatusJson.filename, "r");
    TrackJson.ptr = fopen(TrackJson.filename, "r");
 
    if (NULL == StatusJson.ptr ) {
        printf("Can't open Status.json \n");
    }

    if (NULL == TrackJson.ptr) {
        printf("Can't open Track.json \n");
    }

    //Read the contents of Status.json into a buffer we can use
    populateFileBuffer(&StatusJson);

    //Read the contents of Track.json into a buffer we can use
    populateFileBuffer(&TrackJson);

    fclose(StatusJson.ptr);
    fclose(TrackJson.ptr);

    trackStatus.trackId=1;
    trackStatus.trackState=TRACK_NONE;

    MakeOrionPacket(&PktOut, getGeolocateTelemetryCorePacketID(), 0);    
    OrionCommSend(&PktOut);

    // Wait for confirmation from the gimbal, or 5 seconds - whichever comes first
    while(WaitCount < 9999999999) {
      while ((++WaitCount < 50) && (ProcessData() == FALSE)) usleep(100000);
    //   while ((++WaitCount < 50) && (ProcessData(&StatusJson,&TrackJson) == FALSE)) usleep(100000);
    }

    // If we timed out waiting, tell the user and return an error code
    // if (WaitCount >= 50) KillProcess("Gimbal failed to respond", -1);

    // Done
    return 0;

}// main

// static BOOL ProcessData(filething *StatusJson,filething *TrackJson)
static BOOL ProcessData(void)
{
    // Loop through any new incoming packets
    while (OrionCommReceive(&PktIn))
    {
        printf("Packet recv'd\n");
        // If this is a response to the request we just sent
        if (PktIn.ID == getGeolocateTelemetryCorePacketID())
        {
            // printf(" Packet id: %d  Type:GeolocateTelemetryCore_t\n",PktIn.ID);
            GeolocateTelemetryCore_t core;
            char* coreChar = ((char*)&core);

            // If the cameras packet decodes properly
            if (decodeGeolocateTelemetryCorePacketStructure(&PktIn, &core))
            {
                printf("    Decoded!\n");
                // printf("    pan/tilt:  %1.3f /  %1.3f\n", core.pan, core.tilt);
                // printf("    hfov/vfov: %1.3f /  %1.3f\n", core.hfov, core.vfov);
                
                char TypeString[16];
                switch (core.mode) {
                  case ORION_MODE_DISABLED: strcpy(TypeString, "Disabled"); break;
                  case ORION_MODE_TRACK: strcpy(TypeString, "Track"); break;
                  case ORION_MODE_SCENE: strcpy(TypeString, "Scene"); break;
                  default:               strcpy(TypeString, "Unknown"); break;
                }
                printf("    mode: %-7s \n", TypeString);

                //Send out raw packets
                for(int cot=0;cot<rawSenders.destinationCount;cot++) {
                    printf("Sending to %d\n",cot);
                    udpsenderSend(&rawSenders.sender[cot],coreChar,sizeof(core));
                }

                //Deal with Status and Track Messages
                #define MSG_ID 0 
                char id[4];
                sprintf(id,"%3d",PktIn.ID);
                writeToBufferField(&StatusJson,MSG_ID,id);
                writeToBufferField(&TrackJson,MSG_ID,id);

                #define MSG_LAT 1 
                char latlong[10];
                sprintf(latlong,"%9.6lf",core.posLat);
                writeToBufferField(&StatusJson,MSG_LAT,latlong);
                writeToBufferField(&TrackJson,MSG_LAT,latlong);

                #define MSG_LONG 2 
                sprintf(latlong,"%9.6lf",core.posLon);
                writeToBufferField(&StatusJson,MSG_LONG,latlong);
                writeToBufferField(&TrackJson,MSG_LONG,latlong);

                #define MSG_ALT 3 
                char alt[15];
                sprintf(alt,"%14.6lf",core.posAlt);
                writeToBufferField(&StatusJson,MSG_ALT,alt);
                writeToBufferField(&TrackJson,MSG_ALT,alt);

                #define MSG_HFOV 4 
                char rads[5];
                sprintf(rads,"%5.3f",core.hfov);
                writeToBufferField(&StatusJson,MSG_HFOV,rads);
                writeToBufferField(&TrackJson,MSG_HFOV,rads);

                #define MSG_VFOV 5 
                // char rads[5];
                sprintf(rads,"%5.3f",core.vfov);
                writeToBufferField(&StatusJson,MSG_VFOV,rads);
                writeToBufferField(&TrackJson,MSG_VFOV,rads);

                #define MSG_PAN 6 
                // char rads[5];
                sprintf(rads,"%6.3f",core.pan);
                writeToBufferField(&StatusJson,MSG_PAN,rads);
                // writeToBufferField(&TrackJson,MSG_PAN,rads);

                #define MSG_TILT 7 
                // char rads[5];
                sprintf(rads,"%6.3f",core.tilt);
                writeToBufferField(&StatusJson,MSG_TILT,rads);
                // writeToBufferField(&TrackJson,MSG_TILT,rads);

                //Send out status packets
                for(int cot=0;cot<statusSenders.destinationCount;cot++) {
                    // printf("Status Message:\n%s\n",StatusJson.buffer);
                    udpsenderSend(&statusSenders.sender[cot],StatusJson.buffer,StatusJson.size);
                }

                int sendTrack=0;
                //printf("TrackStatus %d\n",trackStatus.trackState);
                if(core.mode==ORION_MODE_SCENE)   //We are no longer tracking
                    { 
                    if(trackStatus.trackState==TRACK_NEW || trackStatus.trackState==TRACK_TRACK)
                        {
                        trackStatus.trackState=TRACK_BREAK;
                        sendTrack=1;
                        }
                    else if (trackStatus.trackState==TRACK_BREAK)
                        {
                        trackStatus.trackState=TRACK_NONE;
                        }
                    }
                else  //We are tracking something
                    {
                    if(trackStatus.trackState==TRACK_NONE || trackStatus.trackState==TRACK_BREAK )
                        {
                        trackStatus.trackId++;
                        trackStatus.trackState=TRACK_NEW;
                        }
                    else if(trackStatus.trackState==TRACK_NEW)
                        {
                        trackStatus.trackState=TRACK_TRACK;
                        }  
                    sendTrack=1;
                    }
                if(sendTrack)
                    {
                    #define TRACK_ID 6 
                    char id[7];
                    sprintf(id,"% -6d",trackStatus.trackId);
                    writeToBufferField(&TrackJson,TRACK_ID,id);

                    #define TRACK_STATUS 7 
                    char ts[7];
                    if(trackStatus.trackState==TRACK_NEW)
                        { sprintf(ts,"%6s","\"NEW\" " ); }
                    else if(trackStatus.trackState==TRACK_TRACK)
                        { sprintf(ts,"%6s","\"TRACK\""); }
                    else if(trackStatus.trackState==TRACK_BREAK)
                        { sprintf(ts,"%6s","\"BREAK\""); }
                    writeToBufferField(&TrackJson,TRACK_STATUS,ts);
       
                    // printf("Track Message:\n%s\n",TrackJson.buffer);
                    //udpsenderSend(&trackSender,TrackJson.buffer,TrackJson.size);
                    //Send out Track packets
                    for(int cot=0;cot<statusSenders.destinationCount;cot++) {
                        // printf("Status Message:\n%s\n",StatusJson.buffer);
                        udpsenderSend(&trackSenders.sender[cot],TrackJson.buffer,TrackJson.size);
                    }
                }

                return TRUE;
            }

        }
    }

    // Haven't gotten the response we're looking for yet
    return FALSE;

}// ProcessData

// This function just shuts things down consistently with a nice message for the user
static void KillProcess(const char *pMessage, int Value)
{
    // Print out the error message that got us here
    printf("%s\n", pMessage);
    fflush(stdout);

    // Close down the active file descriptors
    OrionCommClose();

    // Finally exit with the proper return value
    exit(Value);

}// KillProcess

static void ProcessArgs(int argc, char **argv)
{
    if(argc==1) {
        help();
    }

    rawSenders.destinationCount=0;
    statusSenders.destinationCount=0;
    trackSenders.destinationCount=0;

    printf("There are %d arguments.\n",argc);
    for(int cot=2;cot<argc;cot++) {
        // printf(" arg[%d]: %s\n",cot,argv[cot]);
        if(strstr(argv[cot],"-raw=")!=NULL) {
            printf("Found a Raw destination: %s\n",argv[cot]);
            if(rawSenders.destinationCount+1==MAX_SENDERS) {continue;}
            int port = getPort(argv[cot]);
            char addr[32];
            getAddr(addr,argv[cot]);
            // printf("%s -> %d\n",addr,port);
            udpsenderInit(&rawSenders.sender[rawSenders.destinationCount],addr,port);
            rawSenders.destinationCount++;
        } 
        else if(strstr(argv[cot],"-status=")!=NULL) {
            printf("Found a Status destination: %s\n",argv[cot]);
            if(statusSenders.destinationCount+1==MAX_SENDERS) {continue;}
            int port = getPort(argv[cot]);
            char addr[32];
            getAddr(addr,argv[cot]);
            // printf("%s -> %d\n",addr,port);
            udpsenderInit(&statusSenders.sender[statusSenders.destinationCount],addr,port);
            statusSenders.destinationCount++;
        } 
        else if(strstr(argv[cot],"-track=")!=NULL) {
            printf("Found a Track destination: %s\n",argv[cot]);
            if(trackSenders.destinationCount+1==MAX_SENDERS) {continue;}
            int port = getPort(argv[cot]);
            char addr[32];
            getAddr(addr,argv[cot]);
            // printf("%s -> %d\n",addr,port);
            udpsenderInit(&trackSenders.sender[trackSenders.destinationCount],addr,port);
            trackSenders.destinationCount++;
        } else {
            printf("Unsupported argument: %s\n",argv[cot]);
        }
    }

    // If we can't connect to a gimbal, kill the app right now
    if (OrionCommOpen(&argc, &argv) == FALSE)
        KillProcess("", 1);

}// ProcessArgs

/** Filething functions *****************************************************************************/
long int getFileSize(FILE* fp)
{
    long int currentPosition = ftell(fp);

    fseek(fp, 0L, SEEK_END);              // move to the end of the file
    long int res = ftell(fp);             // calculating the size of the file
    fseek(fp, currentPosition, SEEK_SET); //move back to where you were when you stated
  
    return res;
}

// Initialize a filething
void thingInit(filething *thing,char filename[]) {
    // printf("Entering initThing()\n");
    thing->filename=filename;
    // if(thing.ptr!=NULL) {
    //     fclose(thing.ptr);
    // }
    thing->ptr=NULL;
    thing->size=0;
    // if(thing->buffer!=NULL) {  //This is not working
    //      free(thing->buffer);
    //  }
    thing->buffer=NULL;
    // printf("Exiting initThing()\n");
}

/* Traverses the data in the buffer and populates the "fields" that it finds.
*/
void populateFieldData(filething *thing) {
    int fieldCot=-1;
    int fieldIdx=-1;
    int fieldLen=0;
    
    //Scan through the buffer and find locations where strings of '***' chars define fields
    for(int idx=0;idx<thing->size;idx++) {
        // printf("%c",thing->buffer[idx]);
        if(thing->buffer[idx]!='*') {
            if(fieldLen>0 && fieldIdx!=-1) { //We just finished a field
               thing->fields[fieldCot][0]=fieldIdx;
               thing->fields[fieldCot][1]=fieldLen;
            //    printf("Found field %d starting at %d with lengh %d.\n",fieldCot,fieldIdx,fieldLen);
               fieldIdx=-1;
               fieldLen=0;
            }
        } else {
            if(fieldIdx==-1) {  //starts a new field
               fieldCot++;
               fieldIdx=idx;
               fieldLen++;
            } else {
               fieldLen++;
            }
        } 
    }
    //Finish filling out the array with 0 data.
    for(int idx=fieldCot+1;idx<THING_MAX_FIELDS;idx++) {
        thing->fields[idx][0]=0;
        thing->fields[idx][1]=0;
    }
    // for(int idx=0;idx<THING_MAX_FIELDS;idx++) {
    //     printf("%d: %d->%d\n", idx, thing->fields[idx][0],thing->fields[idx][1]);
    // }
    //     printf("\n");
}

/* Reads all data from the file pointer, allocates a buffer for it, and then runs
   the process to find fields in the data.
*/
void populateFileBuffer(filething *thing) {
    if(thing->ptr==NULL) {
        printf("Error. File not open: ");
        return;
    }

    if(thing->size==0) {
        thing->size=getFileSize(thing->ptr);
        // printf("Setting Buffer Size:\n%ld\n",thing->size);
    } 

    fseek(thing->ptr, 0, SEEK_SET);       //Move to the beginning of the file.
    thing->buffer = malloc(thing->size);  //Allocate a buffer for the contents of the file.
    fread(thing->buffer, thing->size, 1, thing->ptr);  //Read the file into the buffer.
    // printf("New Buffer Allocated:\n%s\n",thing.buffer);

    //Set the fields based on what is in the buffer
    populateFieldData(thing);
}

/* Writs a string the the specified field number in the buffer.
*/
void writeToBufferField(filething *thing,int fieldnum,char string[]) {
    if(fieldnum<0 || fieldnum>THING_MAX_FIELDS || thing->fields[fieldnum][THING_FIELD_INDEX]==0) {
        return;
    }
    // printf("%d -> [%s]\n",fieldnum,string);
    for(int idx=0;idx<thing->fields[fieldnum][THING_FIELD_LENGTH];idx++) {
        int offset = thing->fields[fieldnum][THING_FIELD_INDEX]+idx; 
        thing->buffer[offset] = string[idx]; 
    }

}
/** UdpDestination functions *****************************************************************************/
int udpsenderInit(udpsender *sender,char dest_ip[],int dest_port) {
    printf("Configuring Udp Sender -> %s:%d\n",dest_ip,dest_port);
    sender->socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->socket_desc < 0) {
        printf("Error opening socket");
        return EXIT_FAILURE;
    }
    sender->destination.sin_family = AF_INET;
    sender->destination.sin_addr.s_addr = inet_addr(dest_ip);
    sender->destination.sin_port = htons(dest_port);
    return(0);
}

int udpsenderSend(udpsender *sender,char buffer[],int buff_len) {
    if(sender->socket_desc==-1) {
        printf("Socket not open.Unable to send data to %s:%d\n",sender->destination.sin_addr,sender->destination.sin_port);
    }

    if (sendto(sender->socket_desc, buffer, buff_len, 0,(struct sockaddr *)&sender->destination, sizeof(struct sockaddr_in)) < 0) {
        printf("Error in sendto()\n");
        return EXIT_FAILURE;
    }
    // long w = (struct sockaddr *)&sender->destination;
    // printf("Sent data to %s:%d\n",inet_ntoa(w),ntohs(sender->destination.sin_port));
    // printf("SENT DATA!!!------------------------> \n");
    return(0);
}
/*** getAddr()  *******************************************************
*   Get the address portion of the string passed in.
*/
static void getAddr(char addr[],char *arg) {
    char* eqLoc = strrchr(arg,'=');
    if(eqLoc==NULL)
       { addr[0]='\0'; }
    
    strcpy(addr,eqLoc+1);
    char* colonLoc = strrchr(addr,':');
    addr[(int)(colonLoc-addr)]='\0';
    // printf("Addr: %s\n",addr);
}     

/*** getPort()  *******************************************************
 *  Get the port from the address passed in.
 */
static int getPort(char *arg) {
    printf("%s\n",arg);
    char* colonLoc = strrchr(arg,':');
    if(colonLoc==NULL)
       { return(0); }
    char portStr[16];
    strcpy(portStr,colonLoc+1);
    // printf("Port: %s\n",portStr);
    return(atoi(portStr));
}

/*** help() ******/
static void help() {
    printf(
        "Relay - Trillium Gimbal Data Relay\n"
        "\n"
        "Usage\n"
        "   Relay <gimbal ip> [arg] ... [arg]\n"
        "\n"
        "Arguments\n"
        "   -raw=<ip address>:<port>  -  Send raw GeolocateTelemetryCore_t packet\n"
        "   -status=<ip address>:<port>  -  Send Status.json packet\n"
        "   -track=<ip address>:<port>  -  Send Track.json packet\n"
        "\n"
        );
    exit(0);
}