//
// _KrustyBus_h_
//
// Copyright (C) 2017-2023 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE in the top level directory for licensing details
//

#ifndef _SST_KRUSTYBUS_H_
#define _SST_KRUSTYBUS_H_

// -- SST HEADERS
#include <sst/core/sst_config.h>
#include <sst/core/component.h>
#include <sst/core/subcomponent.h>
#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/timeConverter.h>
#include <sst/core/interfaces/simpleNetwork.h>
#include <sst/core/model/element_python.h>
#include <sst/core/interfaces/stdMem.h>

// -- CXX Headers
#include <queue>
#include <map>

namespace SST {
namespace KrustyBus {

// defines what type of endpoint this is, CPU/Host or Memory
typedef enum{
  KB_HOST   = 0x01,
  KB_MEM    = 0x02
}KBEndpoint;

// --------------------------------------------
// KrustyBus Network Messages
//
// This is the actual event payload that will
// be utilized by the Merlin network. This
// means that the CPUs will send this event
// type to your bus component.  Notice that
// we basically encapsulate the memory request
// data into the network packet
// --------------------------------------------
class KrustyBusEvent : public SST::Event{
public:

  typedef enum{
    KB_UNK    = 0x00,
    KB_READ   = 0x01,
    KB_WRITE  = 0x02,
    KB_FLUSH  = 0x03,
    KB_FENCE  = 0x04
  }KBOpcode;

  /// KrustyBusEvent: default constructor
  KrustyBusEvent() : Event() { }

  /// KrustyBusEvent: retrieve the opcode
  uint8_t getOpcode() { return Opcode; }

  /// KrustyBusEvent: retrieve the size
  uint8_t getSize() { return Size; }

  /// KrustyBusEvent: retrieve the address
  uint64_t getAddr() { return Addr; }

  /// KrustyBusEvent: retrieve the data
  uint64_t getData() { return Data; }

  /// KrustyBusEvent: retrieve the src ID
  SST::Interfaces::SimpleNetwork::nid_t getSrc() { return Src; }

  /// KrustyBusEvent: retrieve the endpoint type
  uint8_t getType() { return Type; }

  /// KrustyBusEvent: set the opcode
  void setOpcode(uint8_t Opc){ Opcode = Opc; }

  /// KrustyBusEvent: set the size
  void setSize(uint8_t Sz){ Size = Sz; }

  /// KrustyBusEvent: set the address
  void setAddr(uint64_t A){ Addr = A; }

  /// KrustyBusEvent: set the data
  void setData(uint64_t D){ Data = D; }

  /// KrustyBusEvent: set the source
  void setSrc(SST::Interfaces::SimpleNetwork::nid_t s) { Src = s; }

  /// KrustyBusEvent: set the endpoint type
  void setType(uint8_t T) { Type = T; }

  /// KrustyBusEvent: clone the event
  virtual Event* clone(void) override{
    KrustyBusEvent *ev = new KrustyBusEvent(*this);
    return ev;
  }

private:
  uint8_t Opcode;       ///< KrustyBusEvent: opcode
  uint8_t Size;         ///< KrustyBusEvent: size of the request
  uint8_t Type;         ///< KrustyBusEvent: defines the endpoint type: KBEndpoint
  uint64_t Addr;        ///< KrustyBusEvent: address of the request
  uint64_t Data;        ///< KrustyBusEvent: data for the event
  SST::Interfaces::SimpleNetwork::nid_t Src;  ///< KrustyBusEvent: src id

public:
   void serialize_order(SST::Core::Serialization::serializer &ser) override{
    Event::serialize_order(ser);
    ser &Opcode;
    ser &Size;
    ser &Addr;
    ser &Data;
    ser &Src;
   }

   /// KrustyBusEvent: implement the nic serialization
   ImplementSerializable(SST::KrustyBus::KrustyBusEvent);

};  // end KrustyBusEvent

// --------------------------------------------
// KrustyBus NIC API
//
// This is the basic NIC level API (base API)
// used to create a network interface controller
// --------------------------------------------
class KrustyBusNicAPI : public SST::SubComponent{
public:
  SST_ELI_REGISTER_SUBCOMPONENT_API(SST::KrustyBus::KrustyBusNicAPI);

  /// KrustyBusNicAPI: default constructor
  KrustyBusNicAPI(ComponentId_t id, Params& params) : SubComponent(id) {}

  /// KrustyBusNicAPI: default destructor
  ~KrustyBusNicAPI() {}

  /// KrustyBusNicAPI: registers the event handler with the core
  virtual void setMsgHandler(Event::HandlerBase* handler) = 0;

  /// KrustyBusNicAPI: initializes the network
  virtual void init(unsigned int phase) = 0;

  /// KrustyBusNicAPI: setup the network
  virtual void setup() {}

  /// KrustyBusNicAPI: send a message on the network
  virtual void send(KrustyBusEvent *ev, int dest) = 0;

  /// KrustyBusNicAPI: retrieve the number of potential destinations
  virtual int getNumDestinations() = 0;

  /// KrustyBusNicAPI: return the NIC's network address
  virtual SST::Interfaces::SimpleNetwork::nid_t getAddress() = 0;
};  // end KrustyBusNicAPI

// --------------------------------------------
// KrustyBus NIC Interface
//
// This *implements* our NicAPI from above.
// You can have multiple different types of NICs to
// implement this NicAPI
// --------------------------------------------
class KrustyBusIFace : public KrustyBusNicAPI{
public:
  // Register the subcomponent
  SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
    KrustyBusIFace,
    "KrustyBus",
    "KrustyBusIFace",
    SST_ELI_ELEMENT_VERSION(1,0,0),
    "KrustyBus SimpleNetwork Network Interface",
    SST::KrustyBus::KrustyBusNicAPI
  )

  // Register the parameters
  SST_ELI_DOCUMENT_PARAMS(
    {"clockFreq",   "Frequency of period (with units) of the clock", "1GHz" },
    {"port", "Port to use, if loaded as an anonymous subcomponent", "network"},
    {"verbose", "Verbosity for output (0 = nothing)", "0"}
  )

  // Register the ports
  SST_ELI_DOCUMENT_PORTS(
    {"network", "Port to network", {"simpleNetworkExample.nicEvent"} }
  )

  // Register the subcomponent slots
  SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS(
    {"iface", "SimpleNetwork interface to a network", "SST::Interfaces::SimpleNetwork"}
  )

  SST_ELI_DOCUMENT_STATISTICS(
  // none defined
  )

  /// KrustyBusIFace: defualt constuctor
  KrustyBusIFace(ComponentId_t id, Params& params);

  /// KrustyBusIFace: default destructor
  ~KrustyBusIFace();

  /// KrustyBusIFace: callback to parent on received messages
  virtual void setMsgHandler(Event::HandlerBase* handler);

  /// KrustyBusIFace: init function
  virtual void init(unsigned int phase);

  /// KrustyBusIFace: setup function
  virtual void setup();

  /// KrustyBusIFace: send to the destination id
  virtual void send(KrustyBusEvent *ev, int dest);

  /// KrustyBusIFace: retrieve the number of destinations
  virtual int getNumDestinations();

  /// KrustyBusIFace: get the endpoint's network id
  virtual SST::Interfaces::SimpleNetwork::nid_t getAddress();

  /// KrustyBusIFace: callback function for SimpleNetwork
  bool msgNotify(int virtualNetwork);

  /// KrustyBusIFace: clock function
  virtual bool clock(Cycle_t cycle);

protected:
  SST::Output out;                        ///< KrustyBusIFace: SST output object
  SST::Interfaces::SimpleNetwork * iFace; ///< KrustyBusIFace: SST network interface
  SST::Event::HandlerBase *msgHandler;    ///< KrustyBusIFace: SST message handler
  bool initBroadcastSent;                 ///< KrustyBusIFace: Has the init bcast message been sent?
  int numDest;                            ///< KrustyBusIFace: number of SST destinations
  std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQ; ///< KrustyBusIFace: buffered send queue
  std::map<SST::Interfaces::SimpleNetwork::nid_t,uint8_t> endpointTypes;  ///<KrustyBusIFace: map of nid_t to endpoint type

private:
  // Parameters
  std::string ClockFreq;      ///< KrustyBusIFace: clock frequency

};  // end KrustyBusIFace

// --------------------------------------------
// KrustyBus Memory NIC Interface
//
// This *implements* our NicAPI from above.
// You can have multiple different types of NICs to
// implement this NicAPI
// --------------------------------------------
class KrustyBusMemIFace : public KrustyBusNicAPI{
public:
  // Register the subcomponent
  SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
    KrustyBusMemIFace,
    "KrustyBus",
    "KrustyBusMemIFace",
    SST_ELI_ELEMENT_VERSION(1,0,0),
    "KrustyBus SimpleNetwork Network Interface",
    SST::KrustyBus::KrustyBusNicAPI
  )

  // Register the parameters
  SST_ELI_DOCUMENT_PARAMS(
    {"clockFreq",   "Frequency of period (with units) of the clock", "1GHz" },
    {"port", "Port to use, if loaded as an anonymous subcomponent", "network"},
    {"verbose", "Verbosity for output (0 = nothing)", "0"}
  )

  // Register the ports
  SST_ELI_DOCUMENT_PORTS(
    {"network", "Port to network", {"simpleNetworkExample.nicEvent"} }
  )

  // Register the subcomponent slots
  SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS(
    {"iface", "SimpleNetwork interface to a network", "SST::Interfaces::SimpleNetwork"}
  )

  SST_ELI_DOCUMENT_STATISTICS(
  // none defined
  )

  /// KrustyBusMemIFace: defualt constuctor
  KrustyBusMemIFace(ComponentId_t id, Params& params);

  /// KrustyBusMemIFace: default destructor
  ~KrustyBusMemIFace();

  /// KrustyBusMemIFace: callback to parent on received messages
  virtual void setMsgHandler(Event::HandlerBase* handler);

  /// KrustyBusMemIFace: init function
  virtual void init(unsigned int phase);

  /// KrustyBusMemIFace: setup function
  virtual void setup();

  /// KrustyBusMemIFace: send to the destination id
  virtual void send(KrustyBusEvent *ev, int dest);

  /// KrustyBusMemIFace: retrieve the number of destinations
  virtual int getNumDestinations();

  /// KrustyBusMemIFace: get the endpoint's network id
  virtual SST::Interfaces::SimpleNetwork::nid_t getAddress();

  /// KrustyBusMemIFace: callback function for SimpleNetwork
  bool msgNotify(int virtualNetwork);

  /// KrustyBusMemIFace: clock function
  virtual bool clock(Cycle_t cycle);

protected:
  SST::Output out;                        ///< KrustyBusMemIFace: SST output object
  SST::Interfaces::SimpleNetwork * iFace; ///< KrustyBusMemIFace: SST network interface
  SST::Event::HandlerBase *msgHandler;    ///< KrustyBusMemIFace: SST message handler
  bool initBroadcastSent;                 ///< KrustyBusMemIFace: Has the init bcast message been sent?
  int numDest;                            ///< KrustyBusMemIFace: number of SST destinations
  std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQ; ///< KrustyBusMemIFace: buffered send queue
  std::map<SST::Interfaces::SimpleNetwork::nid_t,uint8_t> endpointTypes;  ///<KrustyBusMemIFace: map of nid_t to endpoint type

private:
  // Parameters
  std::string ClockFreq;      ///< KrustyBusMemIFace: clock frequency

};  // end KrustyBusMemIFace

// --------------------------------------------
// KrustyBus Memory Interface
//
// Implements the SimpleNetwork to StandardMem
// translation
// --------------------------------------------
class KrustyMem : public SST::Component{
public:
  // register the component
  SST_ELI_REGISTER_COMPONENT(
    KrustyMem,
    "KrustyBus",
    "KrustyMem",
    SST_ELI_ELEMENT_VERSION(1,0,0),
    "KrustyMem: memHierarchy interface for KrustyBus",
    COMPONENT_CATEGORY_MEMORY
  )

  // document the parameters
  SST_ELI_DOCUMENT_PARAMS(
    { "clockFreq",   "Frequency of period (with units) of the clock", "1GHz" }
  )

  // document the ports
  SST_ELI_DOCUMENT_PORTS()

  // document the statistics
  SST_ELI_DOCUMENT_STATISTICS(
  )

  // document the subcomponent slots
  SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS(
    {"network", "Network interface", "SST::KrustyBus::KrustyBusMemIFace"},
  )

  // -- class members --

  /// KrustyMem: constructor
  KrustyMem(SST::ComponentId_t, SST::Params& params);

  /// KrustyMem: destructor
  ~KrustyMem();

  /// KrustyMem: setup function
  void setup();

  /// KrustyMem: finish function
  void finish();

  /// KrustyMem: init function
  void init();

private:

  /// KrustyMem: clock handler
  bool clock(SST::Cycle_t cycle);

  // KrustyMem: handle the incoming network message
  void handleMessage(SST::Event *ev);

  /// Params
  SST::Output out;            // SST Output object for printing, messaging, etc

  // -- subcomponents --
  KrustyBusNicAPI *Nic;       ///< KrustyBus::KrustyBusNicAPI network interface controller

};  // end KrustyMem


} // namespace KrustyBus
} // namespace SST

#endif  // _SST_KRUSTYBUS_H_
// EOF
