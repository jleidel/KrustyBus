//
// _KrustyBus_cc_
//
// Copyright (C) 2017-2023 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE in the top level directory for licensing details
//

#include "KrustyBus.h"

using namespace SST;
using namespace SST::KrustyBus;

// -------------------------------------------------
// KrustyBusIFace
// -------------------------------------------------
KrustyBusIFace::KrustyBusIFace(ComponentId_t id, Params& params)
  : KrustyBusNicAPI(id, params){
  // setup the output handler
  const int verbosity = params.find<int>("verbose",0);
  out.init("KrustyBusIFace[" + getName() + ":@p:@t]: ", verbosity, 0, SST::Output::STDOUT);

  // read the params
  ClockFreq = params.find<std::string>("clockFreq", "1GHz");

  // register the clock
  registerClock(ClockFreq, new Clock::Handler<KrustyBusIFace>(this,&KrustyBusIFace::clock));

  // load the SimpleNetwork interfaces
  iFace = loadUserSubComponent<SST::Interfaces::SimpleNetwork>("iface",
                                                               ComponentInfo::SHARE_NONE,
                                                               1);

  if( !iFace ){
    // load the anonymous NIC
    Params netparams;
    netparams.insert("port_name", params.find<std::string>("port", "network"));
    netparams.insert("input_buf_size", "64B");
    netparams.insert("output_buf_size", "64B");
    netparams.insert("link_bw", "40GiB/s");
    iFace = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>("merlin.linkcontrol",
                                                                      "iface",
                                                                      0,
                                                                      ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS,
                                                                      netparams,
                                                                      1);
  }

  iFace->setNotifyOnReceive(
    new SST::Interfaces::SimpleNetwork::Handler<KrustyBusIFace>(this,
                                                         &KrustyBusIFace::msgNotify));

  initBroadcastSent = false;
  numDest = 0;
  msgHandler = nullptr;
}

KrustyBusIFace::~KrustyBusIFace(){
}

void KrustyBusIFace::setMsgHandler(Event::HandlerBase* handler){
  msgHandler = handler;
}

void KrustyBusIFace::init(unsigned int phase){
  if( phase == 1){
    out.verbose(CALL_INFO, 8, 0, "Initializing the NIC\n");
  }
  iFace->init(phase);
  if( iFace->isNetworkInitialized() ){
    if( !initBroadcastSent) {
      initBroadcastSent = true;
      KrustyBusEvent *ev = new KrustyBusEvent();
      ev->setType(KB_HOST);
      ev->setSrc(iFace->getEndpointID());

      SST::Interfaces::SimpleNetwork::Request * req = new SST::Interfaces::SimpleNetwork::Request();
      req->dest = SST::Interfaces::SimpleNetwork::INIT_BROADCAST_ADDR;
      req->src = iFace->getEndpointID();
      req->givePayload(ev);
      iFace->sendInitData(req);
    }
  }

  while( SST::Interfaces::SimpleNetwork::Request * req = iFace->recvInitData() ){
    KrustyBusEvent *ev = static_cast<KrustyBusEvent*>(req->takePayload());

    // with this, we register the network ID to the endpoint type in a map
    // this is basically our static routing table such that we know where the
    // memory endpoint is on the network
    endpointTypes[ev->getSrc()] = ev->getType();

    numDest++;
    delete req;
    delete ev;
    out.verbose(CALL_INFO, 9, 0,
                    "%s received init message\n",
                    getName().c_str());
  }
}

void KrustyBusIFace::setup(){
  out.verbose(CALL_INFO, 8, 0, "Setup the NIC\n");
  if( msgHandler == nullptr ){
    out.fatal(CALL_INFO, -1,
               "%s, Error: KrustyBusIFace implements a callback-base notification and parent has not registered the callback function\n",
               getName().c_str());
  }
}

bool KrustyBusIFace::msgNotify(int vn){
  SST::Interfaces::SimpleNetwork::Request* req = iFace->recv(0);

  if( req != nullptr ){
    KrustyBusEvent *ev = static_cast<KrustyBusEvent*>(req->takePayload());
    if( !ev ){
      out.fatal(CALL_INFO, -1,
                 "%s, Error: KrustyBusEvent on KrustyBusIFace is null\n",
                 getName().c_str());
    }
    out.verbose(CALL_INFO, 9, 0,
                 "%s received message from %lld\n",
                 getName().c_str(), (long long)(ev->getSrc()));
    (*msgHandler)(ev);  // <========== this is where we hand off the event payload to our local logic
    delete req;
    delete ev;
  }

  return true;
}

void KrustyBusIFace::send(KrustyBusEvent* event, int destination){
  out.verbose(CALL_INFO, 9, 0,
               "%s sent message of type=%d to %d\n",
               getName().c_str(), event->getOpcode(), destination);
  if( event->getSrc() == -1 ){
    out.fatal(CALL_INFO, -1, "Error: source ID is; Opc=%d; SourceName=%s; destination=%d\n",
              event->getOpcode(), getName().c_str(), destination);
  }
  SST::Interfaces::SimpleNetwork::Request *req = new SST::Interfaces::SimpleNetwork::Request();
  req->dest = destination;
  req->src = iFace->getEndpointID();
  req->givePayload(event);
  sendQ.push(req);
}

int KrustyBusIFace::getNumDestinations(){
  return numDest;
}

SST::Interfaces::SimpleNetwork::nid_t KrustyBusIFace::getAddress(){
  return iFace->getEndpointID();
}

bool KrustyBusIFace::clock(Cycle_t cycle){
  while( !sendQ.empty() ){
    if( iFace->spaceToSend(0,256) && iFace->send(sendQ.front(),0)) {
      sendQ.pop();
      out.verbose(CALL_INFO, 10, 0, "%s flushed a message to the network\n",
                   getName().c_str());
    }else{
      break;
    }
  }

  return false;
}

// -------------------------------------------------
// KrustyBusMemIFace
// -------------------------------------------------
KrustyBusMemIFace::KrustyBusMemIFace(ComponentId_t id, Params& params)
  : KrustyBusNicAPI(id, params){
  // setup the output handler
  const int verbosity = params.find<int>("verbose",0);
  out.init("KrustyBusMemIFace[" + getName() + ":@p:@t]: ", verbosity, 0, SST::Output::STDOUT);

  // read the params
  ClockFreq = params.find<std::string>("clockFreq", "1GHz");

  // register the clock
  registerClock(ClockFreq, new Clock::Handler<KrustyBusMemIFace>(this,&KrustyBusMemIFace::clock));

  // load the SimpleNetwork interfaces
  iFace = loadUserSubComponent<SST::Interfaces::SimpleNetwork>("iface",
                                                               ComponentInfo::SHARE_NONE,
                                                               1);

  if( !iFace ){
    // load the anonymous NIC
    Params netparams;
    netparams.insert("port_name", params.find<std::string>("port", "network"));
    netparams.insert("input_buf_size", "64B");
    netparams.insert("output_buf_size", "64B");
    netparams.insert("link_bw", "40GiB/s");
    iFace = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>("merlin.linkcontrol",
                                                                      "iface",
                                                                      0,
                                                                      ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS,
                                                                      netparams,
                                                                      1);
  }

  iFace->setNotifyOnReceive(
    new SST::Interfaces::SimpleNetwork::Handler<KrustyBusMemIFace>(this,
                                                         &KrustyBusMemIFace::msgNotify));

  initBroadcastSent = false;
  numDest = 0;
  msgHandler = nullptr;
}

KrustyBusMemIFace::~KrustyBusMemIFace(){
}

void KrustyBusMemIFace::setMsgHandler(Event::HandlerBase* handler){
  msgHandler = handler;
}

void KrustyBusMemIFace::init(unsigned int phase){
  if( phase == 1){
    out.verbose(CALL_INFO, 8, 0, "Initializing the NIC\n");
  }
  iFace->init(phase);
  if( iFace->isNetworkInitialized() ){
    if( !initBroadcastSent) {
      initBroadcastSent = true;
      KrustyBusEvent *ev = new KrustyBusEvent();
      ev->setType(KB_MEM);      // <<==================== NOTICE THAT THIS IS DIFFERENT!!!
      ev->setSrc(iFace->getEndpointID());

      SST::Interfaces::SimpleNetwork::Request * req = new SST::Interfaces::SimpleNetwork::Request();
      req->dest = SST::Interfaces::SimpleNetwork::INIT_BROADCAST_ADDR;
      req->src = iFace->getEndpointID();
      req->givePayload(ev);
      iFace->sendInitData(req);
    }
  }

  while( SST::Interfaces::SimpleNetwork::Request * req = iFace->recvInitData() ){
    KrustyBusEvent *ev = static_cast<KrustyBusEvent*>(req->takePayload());

    // with this, we register the network ID to the endpoint type in a map
    // this is basically our static routing table such that we know where the
    // memory endpoint is on the network
    endpointTypes[ev->getSrc()] = ev->getType();

    numDest++;
    delete req;
    delete ev;
    out.verbose(CALL_INFO, 9, 0,
                    "%s received init message\n",
                    getName().c_str());
  }
}

void KrustyBusMemIFace::setup(){
  out.verbose(CALL_INFO, 8, 0, "Setup the NIC\n");
  if( msgHandler == nullptr ){
    out.fatal(CALL_INFO, -1,
               "%s, Error: KrustyBusMemIFace implements a callback-base notification and parent has not registered the callback function\n",
               getName().c_str());
  }
}

bool KrustyBusMemIFace::msgNotify(int vn){
  SST::Interfaces::SimpleNetwork::Request* req = iFace->recv(0);

  if( req != nullptr ){
    KrustyBusEvent *ev = static_cast<KrustyBusEvent*>(req->takePayload());
    if( !ev ){
      out.fatal(CALL_INFO, -1,
                 "%s, Error: KrustyBusEvent on KrustyBusMemIFace is null\n",
                 getName().c_str());
    }
    out.verbose(CALL_INFO, 9, 0,
                 "%s received message from %lld\n",
                 getName().c_str(), (long long)(ev->getSrc()));
    (*msgHandler)(ev);  // <========== this is where we hand off the event payload to our local logic
    delete req;
    delete ev;
  }

  return true;
}

void KrustyBusMemIFace::send(KrustyBusEvent* event, int destination){
  out.verbose(CALL_INFO, 9, 0,
               "%s sent message of type=%d to %d\n",
               getName().c_str(), event->getOpcode(), destination);
  if( event->getSrc() == -1 ){
    out.fatal(CALL_INFO, -1, "Error: source ID is; Opc=%d; SourceName=%s; destination=%d\n",
              event->getOpcode(), getName().c_str(), destination);
  }
  SST::Interfaces::SimpleNetwork::Request *req = new SST::Interfaces::SimpleNetwork::Request();
  req->dest = destination;
  req->src = iFace->getEndpointID();
  req->givePayload(event);
  sendQ.push(req);
}

int KrustyBusMemIFace::getNumDestinations(){
  return numDest;
}

SST::Interfaces::SimpleNetwork::nid_t KrustyBusMemIFace::getAddress(){
  return iFace->getEndpointID();
}

bool KrustyBusMemIFace::clock(Cycle_t cycle){
  while( !sendQ.empty() ){
    if( iFace->spaceToSend(0,256) && iFace->send(sendQ.front(),0)) {
      sendQ.pop();
      out.verbose(CALL_INFO, 10, 0, "%s flushed a message to the network\n",
                   getName().c_str());
    }else{
      break;
    }
  }

  return false;
}



// EOF
