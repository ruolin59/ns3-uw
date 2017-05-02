/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//
// This is an illustration of how one could use virtualization techniques to
// allow running applications on virtual machines talking over simulated
// networks.
//
// The actual steps required to configure the virtual machines can be rather
// involved, so we don't go into that here.  Please have a look at one of
// our HOWTOs on the nsnam wiki for more details about how to get the 
// system confgured.  For an example, have a look at "HOWTO Use Linux 
// Containers to set up virtual networks" which uses this code as an 
// example.
//
// The configuration you are after is explained in great detail in the 
// HOWTO, but looks like the following:
//
//  +----------+                           +----------+
//  | virtual  |                           | virtual  |
//  |  Linux   |                           |  Linux   |
//  |   Host   |                           |   Host   |
//  |          |                           |          |
//  |   eth0   |                           |   eth0   |
//  +----------+                           +----------+
//       |                                      |
//  +----------+                           +----------+
//  |  Linux   |                           |  Linux   |
//  |  Bridge  |                           |  Bridge  |
//  +----------+                           +----------+
//       |                                      |
//  +------------+                       +-------------+
//  | "tap-left" |                       | "tap-right" |
//  +------------+                       +-------------+
//       |           n0            n1           |
//       |       +--------+    +--------+       |
//       +-------|  tap   |    |  tap   |-------+
//               | bridge |    | bridge |
//               +--------+    +--------+
//               |  uan   |    |  uan   |
//               +--------+    +--------+
//                   |             |
//                 ((*))         ((*))
//
//
//
#include <iostream>
#include <fstream>

// General h files
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"

// Tap Bridge Module
#include "ns3/tap-bridge-module.h"

// Uan Module
#include "ns3/uan-module.h"
#include "ns3/applications-module.h"
#include "ns3/log.h"
#include "ns3/config.h"
#include "ns3/callback.h"
#include "ns3/stats-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TapUanVirtualMachineExample");

int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  //
  // We are interacting with the outside, real, world.  This means we have to 
  // interact in real-time and therefore means we have to use the real-time
  // simulator and take the time to calculate checksums.
  //
  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  //
  // Create two ghost nodes.  The first will represent the virtual machine host
  // on the left side of the network; and the second will represent the VM on 
  // the right side.
  //
  NodeContainer nodes;
  nodes.Create (2);

  //
  // Here begins the channel module configurations
  //

  //
  // We're going to use uan.
  //
  UanHelper uan;
  Ptr<UanChannel> chan = CreateObject<UanChannel>();

  //
  // Configure propagation model
  //
  #ifdef UAN_PROP_BH_INSTALLED 
    Ptr<UanPropModelBh> prop = 
      CreateObjectWithAttributes<UanPropModelBh> ("ConfigFile", StringValue("exbhconfig.cfg")); 
  #else 
    Ptr<UanPropModelIdeal> prop = 
      CreateObjectWithAttributes<UanPropModelIdeal> (); 
  #endif //UAN_PROP_BH_INSTALLED 
  chan->SetAttribute ("PropagationModel", PointerValue (prop));

  //
  // Configure modulation mode
  //
  UanTxMode mode; 
  mode = UanTxModeFactory::CreateMode (UanTxMode::FSK, // Modulation type
                                       1624, // Data rate in BPS: 1152B / 5.67s
                                       1624, //
                                       24000, // Center Frequency in Hz
                                       6000, // Bandwidth in Hz
                                       2, // Modulation constellation size, 2 for BPSK, 4 for QPSK 
                                       "Default mode"); // String name for this transmission mode
  UanModesList myModes; 
  myModes.AppendMode (mode); 

  //
  // Configure the physcial layer module for uan channel.
  //
  std::string perModel = "ns3::UanPhyPerGenDefault"; 
  std::string sinrModel = "ns3::UanPhyCalcSinrDefault"; 
  ObjectFactory obf; 
  obf.SetTypeId (perModel); 
  Ptr<UanPhyPer> per = obf.Create<UanPhyPer> (); 
  obf.SetTypeId (sinrModel); 
  Ptr<UanPhyCalcSinr> sinr = obf.Create<UanPhyCalcSinr> ();
  uan.SetPhy ("ns3::UanPhyGen", 
              "PerModel", PointerValue (per), 
              "SinrModel", PointerValue (sinr), 
              "SupportedModes", UanModesListValue (myModes)); 

  //
  // Configure the MAC module for uan channel
  //
  uan.SetMac ("ns3::UanMacAloha"); // every nodes transmit at will

  //
  // Install the wireless devices onto our ghost nodes.
  //
  NetDeviceContainer devices = uan.Install (nodes, chan);

  //
  // We need location information since we are talking about wireless communication, so add a
  // constant position to the ghost nodes.
  //
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  positionAlloc->Add (Vector (5.0, 0.0, 0.0));
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (nodes);

  //
  // Use the TapBridgeHelper to connect to the pre-configured tap devices for 
  // the left side.  We go with "UseLocal" mode since the wifi devices do not
  // support promiscuous mode (because of their natures0.  This is a special
  // case mode that allows us to extend a linux bridge into ns-3 IFF we will
  // only see traffic from one other device on that bridge.  That is the case
  // for this configuration.
  //
  TapBridgeHelper tapBridge;
  tapBridge.SetAttribute ("Mode", StringValue ("UseLocal"));
  tapBridge.SetAttribute ("DeviceName", StringValue ("tap-vNode1"));
  tapBridge.Install (nodes.Get (0), devices.Get (0));

  //
  // Connect the right side tap to the right side wifi device on the right-side
  // ghost node.
  //
  tapBridge.SetAttribute ("DeviceName", StringValue ("tap-vNode2"));
  tapBridge.Install (nodes.Get (1), devices.Get (1));

  //
  //
  //Simulator::Stop (Seconds (600.));
  Simulator::Run ();
  Simulator::Destroy ();
}
