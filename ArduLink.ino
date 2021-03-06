// 0.0.1 early functionality
// character builder reset also resets packetizer (means something was mangled)
#include <Arduino.h>

#undef RECEIVE
#define TINY
#undef UNO

#undef DEBUG 
#undef DEBUG0

#define PULSEWIDTH 100
#define PULSEGAP 100
#define PULSEFRAME 1000

#ifdef UNO
#include <Serial.h>
int semicycle=26;
int statusLED=4;
int sensorPin=2; //we need an interrupt
int signalLED=3;
int switchPin=7;
#endif

#ifdef TINY
int semicycle=28;
int statusLED=1;
int signalLED=0;
int sensorPin=3;
int switchPin=4;
#endif

int cycle=semicycle*2;
int jitter=semicycle/4;


#ifdef RECEIVE
unsigned long int t0; //previous transition
unsigned long int t=0;    //current transition
unsigned char incoming=0;
boolean packetAvailable=false;
unsigned char packetLength;
char incomingBuffer[256];
#endif

void selfTest(){
  unsigned short int t=128;
  boolean state=true;
  while (t>0){
    digitalWrite(statusLED,state);
    digitalWrite(signalLED,state);
    delay(t);
    state=!state;
    t/=2;
#ifdef DEBUG
    Serial.print(t);
    Serial.print(" ");
#endif
  }

}

void setup(){
  #ifdef UNO
  Serial.begin(57600);
  #endif
  pinMode(statusLED,OUTPUT);
  pinMode(signalLED,OUTPUT);
  pinMode(switchPin,INPUT);
  digitalWrite(switchPin,HIGH);
  pinMode(sensorPin,INPUT);
  selfTest();
  #ifdef RECEIVE
  attachInterrupt(0,crossover,CHANGE);
  #endif
}



#ifdef RECEIVE
void crossover(){ //ISR for sensor pin change
  static unsigned char state=0;
  static unsigned int bitcount=0;
  static boolean resetOccurred=false; //did the character builder reset at some point?
  unsigned long int deltaT; //watch out for the datatype size

  int tmp;

  t0=t;
  t=millis();
  deltaT=t-t0;
  if (deltaT>cycle*2) {
    state=0;
    resetOccurred=true;
    #ifdef DEBUG
    Serial.print(deltaT);
    Serial.println("delay, char builder reset.");
    #endif
  } //autoreset after a silence: assume sender has died
#ifdef DEBUG0
  Serial.print(deltaT);
  Serial.print("  ");
  Serial.print(state);
  Serial.print("  ");
#endif
  switch(state) {

  case 0:
    bitcount=0;
    state=1;
    break;

  case 1:
    //one her short pulse
    if (abs(deltaT-semicycle)<jitter){
      state=2;
    }
    break;

  case 2: //begin with actual data
    tmp=deltaT-semicycle;
#ifdef DEBUG0
    Serial.print(" ");
#endif
    if(abs(tmp)<jitter){ //semiperiod, we have read a 1
#ifdef DEBUG0
      Serial.print(1);
#endif
      incoming = (incoming<<1) | 1; //global var, the character being built
      bitcount++;
      state=3;
    }
    else if (abs(tmp-semicycle)<jitter) { //full period, we have read a 0, we are ready for the next symbol
#ifdef DEBUG0
      Serial.print(0);
#endif
      incoming = (incoming<<1); 
      bitcount++;
      state=2;
    }
    else{
      resetOccurred=true;
      state=0; //decoding failed, go back to zero
    }
    break;

  case 3: //if we have read a one, there will be one more transition inside the symbol
    if(abs(tmp)<jitter){ //semiperiod
      state=2;
    }
    else{
      resetOccurred=true;
      state=0;
    } //decoding failed
    break;

  default:
    #ifdef DEBUG
    Serial.print("Oy vey!");
    #endif
    break;
  } 
  if (bitcount==8){
#ifdef DEBUG
    Serial.println();
    Serial.print("read = ");
    Serial.print(incoming,DEC);
    Serial.println();
#endif
    bitcount=0;
    //state=2;
    processCharacter(incoming, deltaT,resetOccurred); //call the packet builder
    resetOccurred=false; //if we have gotten so far, we have a successful character, so no reset.
    incoming=0; //not strictly useful, no...
  }
#ifdef DEBUG0
  Serial.println();
#endif
}

void processCharacter(char c, int deltaT, boolean resetOccurred){
  static unsigned char state=0;
  static unsigned char checksum=0;
  static unsigned char incomingCharIdx=0;

#ifdef DEBUG
  Serial.print(state);
  Serial.print(" ");
  Serial.print(c,DEC);
  Serial.print(" ");
  Serial.print(incomingCharIdx);
  Serial.print(" ");
  Serial.print(checksum);
  Serial.println();
#endif 

  //Reset packet building if the transition is arriving after a "long" time
  //each byte is one whole cycle
  if (resetOccurred || (deltaT>cycle*16)){
    state=0;
    checksum=0;
    #ifdef DEBUG
    Serial.print("Packetizer reset deltaT= ");
    Serial.println(deltaT);
    Serial.print("  >>");
    Serial.print(incomingBuffer);
    Serial.println("<<");
    #endif
  }

  checksum+=c;
  switch (state){

  case 0:
    packetAvailable=false;
    incomingCharIdx=0;
    //first byte is packet length
    packetLength=c;
    state=1;
    break;

  case 1:
    state=2; //second byte is parity
    break;

  case 2:
    incomingBuffer[incomingCharIdx++]=c; //write character into buffer
    if (incomingCharIdx==packetLength) {
      if (checksum==0){
        packetAvailable=true;
        incomingBuffer[incomingCharIdx]=0; //courtesy zero terminate, a valid string
        state=0;
      }
      else{
        state=0;
      }
    }
    break;
  }

}
#endif 

void status(unsigned char c){
  unsigned int t=millis();
  unsigned short int t1=t%128;
  if((1<<((t/128)%8))&c){ //true if this timeslot is a "true" one
    if (t1>16 && t1<248){
      digitalWrite(statusLED,HIGH);
    }
  } 
  else if (t1>112 && t1<144){
    digitalWrite(statusLED,HIGH);
  }
  else {
    digitalWrite(statusLED,LOW);
  }
}




void signal(boolean value){
  static boolean boyd = true;
  static unsigned long int t=millis();
  if (value==false) boyd=false;
  digitalWrite(signalLED,boyd);
  /*
  Serial.print("-------------------");
   Serial.print(millis()-t,DEC);
   Serial.print(" ");
   t=millis();
   */
  #ifdef debug0
  Serial.println(boyd?"1":"0");
  #endif
  
  boyd = !boyd;

}

void send_start(){
  signal(true); //HIGH
  delay(semicycle); //state 1
  signal(true); //LOW
  delay(semicycle);
  //end with lamp off
}

void send(unsigned char c){
  static boolean lampstate=false;
#ifdef DEBUG
  Serial.print(c,DEC);
  Serial.print(" ");
  Serial.print(c,BIN);
  Serial.print(" ");
  Serial.print(" ");
#endif
  for(int i=7;i>=0;i--){
    if(c & (1<<i)){ //the ith bit is set we need to flip at the beginning of the symbol
      lampstate = !lampstate;
      signal(true);
#ifdef DEBUG
      Serial.print(1);
#endif
    } 
    else {
#ifdef DEBUG
      Serial.print(0);
#endif
    }

    delay(semicycle); //wait another half cycle
    lampstate = !lampstate;
    signal(true);
    delay(semicycle);
  }
#ifdef DEBUG
  Serial.println();
#endif

  //digitalWrite(signalLED,LOW); //conclude the character
  //digitalWrite(statusLED,LOW);

}

void send1(unsigned char c){
  send_start();
  send(c);
}

void sends(char *s){
  //requires call to send_start() beforehand. Or nothing will work.
  digitalWrite(statusLED,HIGH);
  for (int i = 0; s[i] != 0; i++){
    send(s[i]);
  }
  delay(cycle*2);
  signal(LOW);
  digitalWrite(statusLED,LOW);
}

/*
Packet structure
 
 [LEN][PAR][DATA][DATA][DATA...]
 
 LEN is the number of bytes in the data. It does not include LEN and PAR
 PAR is the parity of the whole packet including LEN. A valid packet must XOR to zero
 DATA is up to 256 data bytes
 
 [003 [   ] [00
 */

void sendPacket(char *s){
  unsigned char len=0;
  unsigned char checksum=0;
  //count the characters in s and accumulate checksum by XORing values

  while(s[len]!=0){
    #ifdef DEBUG
    Serial.println();
    Serial.print(len,DEC);
    Serial.print(" ");
    Serial.print(s[len],DEC);
    #endif
    checksum += s[len];
    len++;
  }
  
  checksum +=len; //make sure length is in the checksum too
  checksum = 256-checksum; //this way the whole packet sums to zero
  send_start();
  send(len);
  send(checksum);
  sends(s);
#ifdef DEBUG
  Serial.println();
  Serial.print("Packet length ");
  Serial.println(len,DEC);
  Serial.print("Checksum ");
  Serial.println(checksum,DEC);
#endif
}


void sendPulseTDM(char c, boolean wait_for_channel){
  for(int i=7;i>=0;i--){
    if(c & (1<<i)){ //the ith bit is set
      digitalWrite(signalLED,HIGH);
      delayMicroseconds(PULSEWIDTH);
      digitalWrite(signalLED,LOW);
#ifdef DEBUG
      Serial.print(1);
#endif
    } 
    else {
      delayMicroseconds(PULSEWIDTH+PULSEGAP);
#ifdef DEBUG
      Serial.print(0);
#endif
    }
    delay(PULSEFRAME);
  }
}


boolean switchDown(){
  boolean state=digitalRead(switchPin);
  static boolean oldstate=state;
  if (state!=oldstate){
    oldstate=state;
    return (state==HIGH);
  }
  return false;
}



void loop(){ 
  #ifdef UNO
  char st[128];
  if (switchDown()){
    for (int j=0;j<32;j++){
      for (int i=0;i<8;i++){
        st[i]=65+(i+j)%26;
      }
      st[5]=0;
      Serial.println();
      Serial.println(st);
      Serial.println();
      sendPacket(st);
      delay(512);
    }
  }
  #endif
 
  #ifdef TINY
  delay(256);
  char test[]="Tiny ";
  test[4]=65+analogRead(1)/64;
  sendPacket(test);
  #endif


  #ifdef UNO
  delay(2);
  if (packetAvailable){
    digitalWrite(statusLED,HIGH);
    delay(50);
    Serial.println(incomingBuffer);
    digitalWrite(statusLED,LOW);
    packetAvailable=false;
  }
  #endif
}






















