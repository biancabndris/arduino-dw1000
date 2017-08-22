/*
 * ConnectedRanging.cpp
 *
 *  Created on: Aug 18, 2017
 *      Author: steven <stevenhelm@live.nl>
 */


#include "ConnectedRanging.h"

ConnectedRangingClass ConnectedRanging;

// data buffer. Format of data buffer is like this: {from_address, to_address_1, message_type, additional_data, to_address_2, message_type, additional_data,.... to_address_n, message_type, additional data}
// Every element takes up exactly 1 byte, except for additional_data, which takes up 15 bytes in the case of a RANGE message, and 4 bytes in the case of RANGE_REPORT
//byte ConnectedRangingClass::_data[MAX_LEN_DATA] = {2, 1, POLL, 3, RANGE, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 4, RANGE_REPORT, 1, 2, 3, 4, 5, POLL_ACK};
byte ConnectedRangingClass::_data[MAX_LEN_DATA];

// pins on the arduino used to communicate with DW1000
uint8_t ConnectedRangingClass::_RST;
uint8_t ConnectedRangingClass::_SS;
uint8_t ConnectedRangingClass::_IRQ;

// addresses of current DW1000
byte ConnectedRangingClass::_longAddress[LEN_EUI];
byte ConnectedRangingClass::_veryShortAddress;

// message sent/received state
volatile boolean ConnectedRangingClass::_sentAck     = false;
volatile boolean ConnectedRangingClass::_receivedAck = false;

// keeping track of send times
uint32_t ConnectedRangingClass::_lastSent = 0;

// nodes (_numNodes includes the current device, but _networkNodes does not)
DW1000Node ConnectedRangingClass::_networkNodes[MAX_NODES];
uint8_t ConnectedRangingClass::_numNodes = 0;

// when it is time to send
boolean ConnectedRangingClass::_timeToSend = false;

// remembering future time in case RANGE message is sent
boolean ConnectedRangingClass::_rangeSent = false;
DW1000Time ConnectedRangingClass::_rangeTime;




// initialization function
void ConnectedRangingClass::ConnectedRangingClass::init(char longAddress[], uint8_t numNodes){
	// nodes to range to
	if(numNodes<=MAX_NODES){
		_numNodes = numNodes;
	}
	else{
		Serial.println("The desired number of nodes exceeds MAX_NODES");
		_numNodes=MAX_NODES;
	}
	DW1000.convertToByte(longAddress, _longAddress);
	initDecawave(_longAddress, numNodes);
	initNodes();
	_lastSent = millis();

}

// initialization function
void ConnectedRangingClass::init(uint8_t veryShortAddress,uint8_t numNodes){
	// nodes to range to
	if(numNodes<=MAX_NODES){
		_numNodes = numNodes;
	}
	else{
		Serial.println("The desired number of nodes exceeds MAX_NODES");
		_numNodes=MAX_NODES;
	}
	for(int i=0;i<LEN_EUI;i++){
		_longAddress[i] = veryShortAddress;
	}
	initDecawave(_longAddress, numNodes);
	initNodes();
	_lastSent = millis();

}

// initialization function
void ConnectedRangingClass::initNodes(){
	byte address[LEN_EUI];
	uint8_t remoteShortAddress = 1;
	for (int i=0;i<_numNodes-1;i++){
		if(remoteShortAddress==_veryShortAddress){
			remoteShortAddress++;
		}
		if(remoteShortAddress!=_veryShortAddress){
			for (int j=0;j<LEN_EUI;j++){
				address[j]=remoteShortAddress;
			}
			DW1000Node temp = DW1000Node(address);
			_networkNodes[i]=temp;
			remoteShortAddress++;
		}

	}
}

// initialization function
void ConnectedRangingClass::initDecawave(byte longAddress[], uint8_t numNodes, const byte mode[], uint16_t networkID,uint8_t myRST, uint8_t mySS, uint8_t myIRQ){
	_RST = myRST;
	_SS = mySS;
	_IRQ = myIRQ;
	DW1000.begin(myIRQ,myRST);
	DW1000.select(mySS);
	//Save the address
	_veryShortAddress = _longAddress[0];
	//Write the address on the DW1000 chip
	DW1000.setEUI(_longAddress);
	Serial.print("very short device address: ");
	Serial.println(_veryShortAddress);

	//Setup DW1000 configuration
	DW1000.newConfiguration();
	DW1000.setDefaults(true);
	DW1000.setDeviceAddress((uint16_t)_veryShortAddress);
	DW1000.setNetworkId(networkID);
	DW1000.enableMode(mode);
	DW1000.commitConfiguration();
	DW1000.attachSentHandler(handleSent);
	DW1000.attachReceivedHandler(handleReceived);
	receiver();

}

void ConnectedRangingClass::loop(){
	if(_sentAck){
		_sentAck = false;
		Serial.println("Sent a message:");
		updateSentTimes();
		for(int i=0; i<MAX_LEN_DATA;i++){
			Serial.print(_data[i]);Serial.print(" ");
		}
		Serial.println(" ");
	}
	if(_receivedAck){
		_receivedAck = false;
		//we read the datas from the modules:
		// get message and parse
		DW1000.getData(_data, MAX_LEN_DATA);
		handleReceivedData();
	}
	if (_veryShortAddress==1 && millis()-_lastSent>DEFAULT_RESET_TIME){
		_lastSent = millis();
		_timeToSend = true;
	}
	if(_timeToSend){
		_timeToSend = false;
		produceMessage();
		if(_rangeSent){
			_rangeSent = false;
			transmitData(_data);
		}
		else{
			DW1000Time delay = DW1000Time(DEFAULT_REPLY_DELAY_TIME, DW1000Time::MICROSECONDS);
			transmitData(_data,delay);
		}
	}

}


void ConnectedRangingClass::loopReceive(){
	if(_receivedAck){
		_receivedAck = false;
		Serial.print("message received: ");
		DW1000.getData(_data,MAX_LEN_DATA);
		char msg[MAX_LEN_DATA];
		DW1000.convertBytesToChars(_data,msg,MAX_LEN_DATA);
		/*
		for(int i=0;i<MAX_LEN_DATA;i++){
			Serial.println(_data[i]);
		}*/
		Serial.println(msg);
	}

}

/*
void ConnectedRangingClass::loopTransmit(char msg[],uint16_t n){
	if(_sentAck){
		_sentAck = false;
		Serial.print("Sent message: ");
		Serial.println(msg);
	}
	if (millis()-_lastSent > transmitDelay){
		_lastSent = millis();
		transmitData(msg,n);
	}

}

void ConnectedRangingClass::loopTransmit(){
	if(_sentAck){
		_sentAck = false;
		Serial.println("message sent!");
	}
	if (millis()-_lastSent > transmitDelay){
		_lastSent = millis();
		_data[0]=68;
		transmitData(_data);
	}

}
*/


void ConnectedRangingClass::receiver() {
	DW1000.newReceive();
	DW1000.setDefaults(true);
	// so we don't need to restart the receiver manually
	DW1000.receivePermanently(true);
	DW1000.startReceive();
}

void ConnectedRangingClass::transmitData(byte datas[]){
	DW1000.newTransmit();
	DW1000.setDefaults(true);
	DW1000.setData(datas,MAX_LEN_DATA);
	DW1000.startTransmit();
}

void ConnectedRangingClass::transmitData(byte datas[], DW1000Time timeDelay){
	DW1000.newTransmit();
	DW1000.setDefaults(true);
	DW1000.setDelay(timeDelay);
	DW1000.setData(datas,MAX_LEN_DATA);
	DW1000.startTransmit();
}

void ConnectedRangingClass::transmitData(char datas[]){
	DW1000.convertCharsToBytes(datas, _data,MAX_LEN_DATA);
	DW1000.newTransmit();
	DW1000.setDefaults(true);
	DW1000.setData(_data,MAX_LEN_DATA);
	DW1000.startTransmit();
}

void ConnectedRangingClass::transmitData(char datas[],uint16_t n){
	DW1000.convertCharsToBytes(datas, _data,n);
	DW1000.newTransmit();
	DW1000.setDefaults(true);
	DW1000.setData(_data,n);
	DW1000.startTransmit();
}


void ConnectedRangingClass::handleSent() {
	// status change on sent success
	_sentAck = true;
}

void ConnectedRangingClass::handleReceived() {
	// status change on received success
	_receivedAck = true;
}

void ConnectedRangingClass::handleReceivedData(){
	uint8_t messagefrom = _data[0];
	Serial.print("Message from: ");Serial.println(messagefrom);
	if (messagefrom==_veryShortAddress-1){
		Serial.println("It is time to send!");
		_timeToSend = true;
	}
	uint16_t datapointer = 1;
	uint8_t toDevice;
	for (int i=0; i<_numNodes-1;i++){
		toDevice = _data[datapointer];
		if(toDevice!=_veryShortAddress){
			incrementDataPointer(&datapointer);
		}
		else if(toDevice==_veryShortAddress){
			processMessage(messagefrom,&datapointer);
		}

	}
}

// if this part of the message was not for the current device, skip the pointer ahead to next block of _data
void ConnectedRangingClass::incrementDataPointer(uint16_t *ptr){
	uint8_t msgtype = _data[*ptr+1];
	switch(msgtype){
		case POLL : *ptr += POLL_SIZE+1;
					break;
		case POLL_ACK : *ptr += POLL_ACK_SIZE+1;
					break;
		case RANGE : *ptr += RANGE_SIZE+1;
					break;
		case RANGE_REPORT : *ptr += RANGE_REPORT_SIZE+1;
					break;
		case RECEIVE_FAILED : *ptr += RECEIVE_FAILED_SIZE+1;
					break;
	}

}

void ConnectedRangingClass::processMessage(uint8_t msgfrom, uint16_t *ptr){
	uint8_t nodeIndex = msgfrom -1 - (uint8_t)(_veryShortAddress<msgfrom);
	DW1000Node *distantNode = &_networkNodes[nodeIndex];
	uint8_t msgtype = _data[*ptr+1];
	*ptr+=2;
	if(msgtype == POLL){
		distantNode->setStatus(2);
		DW1000.getReceiveTimestamp(distantNode->timePollReceived);
		Serial.print(F("POLL processed for distantNode "));Serial.println(distantNode->getShortAddress(),HEX);
	}
	else if(msgtype == POLL_ACK){
		distantNode->setStatus(4);
		DW1000.getReceiveTimestamp(distantNode->timePollAckReceived);
		Serial.print(F("POLL ACK processed for distantNode "));Serial.println(distantNode->getShortAddress(),HEX);
	}
	// ADD THE ADDITIONAL INFO THAT A RANGE MESSAGE CONTAINS, TIME POLL SENT AND TIME RANGE SENT ARE UNKNOWN!
	else if(msgtype == RANGE){
		distantNode->setStatus(6);
		DW1000.getReceiveTimestamp(distantNode->timeRangeReceived);
		distantNode->timePollSent.setTimestamp(_data+*ptr);
		*ptr += 5;
		distantNode->timePollAckReceived.setTimestamp(_data+*ptr);
		*ptr += 5;
		distantNode->timeRangeSent.setTimestamp(_data+*ptr);
		*ptr += 5;
		DW1000Time TOF;
		computeRangeAsymmetric(distantNode, &TOF);
		float distance = TOF.getAsMeters();
		distantNode->setRange(distance);
		Serial.print(F("RANGE processed for distantNode "));Serial.print(distantNode->getShortAddress(),HEX); Serial.print(F(" , Range is: ")); Serial.println(distance);
	}
	else if(msgtype == RANGE_REPORT){
		distantNode->setStatus(0);
		float curRange;
		memcpy(&curRange, _data+*ptr, 4);
		Serial.print(F("RANGE_REPORT processed for distantNode "));Serial.print(distantNode->getShortAddress(),HEX); Serial.print(F(" , Range is: ")); Serial.println(curRange);
	}
	// PROPERLY HANDLE RECEIVE FAILED MESSAGE (NOW IT JUST REPEATS THE PROTOCOL FROM SCRATCH)
	else if(msgtype == RECEIVE_FAILED){
		distantNode->setStatus(0);
		Serial.println("protocol failed at receive");
	}
}

void ConnectedRangingClass::computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF) {
	// asymmetric two-way ranging (more computation intense, less error prone)
	DW1000Time round1 = (myDistantDevice->timePollAckReceived-myDistantDevice->timePollSent).wrap();
	DW1000Time reply1 = (myDistantDevice->timePollAckSent-myDistantDevice->timePollReceived).wrap();
	DW1000Time round2 = (myDistantDevice->timeRangeReceived-myDistantDevice->timePollAckSent).wrap();
	DW1000Time reply2 = (myDistantDevice->timeRangeSent-myDistantDevice->timePollAckReceived).wrap();

	myTOF->setTimestamp((round1*round2-reply1*reply2)/(round1+round2+reply1+reply2));
	/*
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("timePollSent ");myDistantDevice->timePollSent.print();
	Serial.print("round1 "); Serial.println((long)round1.getTimestamp());

	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("timePollReceived ");myDistantDevice->timePollReceived.print();
	Serial.print("reply1 "); Serial.println((long)reply1.getTimestamp());

	Serial.print("timeRangeReceived ");myDistantDevice->timeRangeReceived.print();
	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("round2 "); Serial.println((long)round2.getTimestamp());

	Serial.print("timeRangeSent ");myDistantDevice->timeRangeSent.print();
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("reply2 "); Serial.println((long)reply2.getTimestamp());
	 */
}

void ConnectedRangingClass::updateSentTimes(){
	DW1000Node* distantNode;
	for (int i=0;i<_numNodes-1;i++){
		distantNode = &_networkNodes[i];
		if (distantNode->getStatus()==1){
			DW1000.getTransmitTimestamp(distantNode->timePollSent);
		}
		else if (distantNode->getStatus()==3){
			DW1000.getTransmitTimestamp(distantNode->timePollAckSent);
		}
	}
}

void ConnectedRangingClass::produceMessage(){
	uint16_t datapointer = 0;
	memcpy(_data,&_veryShortAddress,1);
	//Serial.print("very Short Address: ");Serial.print(_veryShortAddress);Serial.print(" , _data[0]: ");
	//Serial.println(_data[0]);
	datapointer++;
	for(int i=0;i<_numNodes-1;i++){
		addMessageToData(&datapointer,&_networkNodes[i]);
	}


}

void ConnectedRangingClass::addMessageToData(uint16_t *ptr, DW1000Node *distantNode){
	switch(distantNode->getStatus()){
	case 0 : addPollMessage(ptr, distantNode); break;
	case 1 : addReceiveFailedMessage(ptr, distantNode); break;
	case 2 : addPollAckMessage(ptr, distantNode); break;
	case 3 : addReceiveFailedMessage(ptr, distantNode); break;
	case 4 : addRangeMessage(ptr, distantNode); break;
	case 5 : addReceiveFailedMessage(ptr, distantNode); break;
	case 6 : addRangeReportMessage(ptr, distantNode); break;
	case 7: addReceiveFailedMessage(ptr,distantNode); break;
	}

}

void ConnectedRangingClass::addPollMessage(uint16_t *ptr, DW1000Node *distantNode){
	distantNode->setStatus(1);
	byte toSend[2] = {distantNode->getVeryShortAddress(),POLL};
	memcpy(_data+*ptr,toSend,2);
	*ptr+=2;
}

void ConnectedRangingClass::addPollAckMessage(uint16_t *ptr, DW1000Node *distantNode){
	distantNode->setStatus(3);
	byte toSend[2] = {distantNode->getVeryShortAddress(),POLL_ACK};
	memcpy(_data+*ptr,toSend,2);
	*ptr+=2;
}

void ConnectedRangingClass::addRangeMessage(uint16_t *ptr, DW1000Node *distantNode){
	distantNode->setStatus(5);
	if(!_rangeSent){
		_rangeSent = true;
		// delay the next message sent because it contains a range message
		DW1000Time deltaTime = DW1000Time(DEFAULT_REPLY_DELAY_TIME,DW1000Time::MICROSECONDS);
		_rangeTime = DW1000.setDelay(deltaTime);
	}
	distantNode->timeRangeSent = _rangeTime;
	byte toSend[2] = {distantNode->getVeryShortAddress(),RANGE};
	memcpy(_data+*ptr,toSend,2);
	*ptr += 2;
	distantNode->timePollSent.getTimestamp(_data+*ptr);
	*ptr += 5;
	distantNode->timePollAckReceived.getTimestamp(_data+*ptr);
	*ptr += 5;
	distantNode->timeRangeSent.getTimestamp(_data+*ptr);
	*ptr += 5;
}

void ConnectedRangingClass::addRangeReportMessage(uint16_t *ptr, DW1000Node *distantNode){
	distantNode->setStatus(7);
	byte toSend[2] = {distantNode->getVeryShortAddress(),RANGE_REPORT};
	memcpy(_data+*ptr,toSend,2);
	*ptr += 2;
	float range = distantNode->getRange();
	memcpy(_data+*ptr,&range,4);
	*ptr += 4;
}

void ConnectedRangingClass::addReceiveFailedMessage(uint16_t *ptr, DW1000Node *distantNode){
	distantNode->setStatus(0);
	byte toSend[2] = {distantNode->getVeryShortAddress(),RECEIVE_FAILED};
	memcpy(_data+*ptr,toSend,2);
	*ptr += 2;
}

