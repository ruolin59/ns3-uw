/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 University of Washington
 *
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

#include "uan-address-translator.h"


namespace ns3 {
  AddressTranslator::AddressTranslator(){
  }

  AddressTranslator::~AddressTranslator(){
    storeMap.clear();
    getMap.clear();
  }

  UanAddress AddressTranslator::translate(const Mac48Address addr){
    /** Special case for broadcast */
    if (addr == Mac48Address ("ff:ff:ff:ff:ff:ff"))
      return UanAddress::GetBroadcast();

    char buff[6];
    addr.CopyTo((uint8_t*) buff);
    std::string key = buff;
    std::unordered_map<std::string,UanAddress>::const_iterator got = storeMap.find (key);
    if ( got != storeMap.end() )
      return got->second;
    
    UanAddress translated = UanAddress::Allocate();
    storeMap[key] = translated;
    
    translated.CopyTo((uint8_t*)buff);
    getMap[(uint8_t)buff[0]] = addr;

    return translated;
  }


  Mac48Address AddressTranslator::getM48(const UanAddress addr){
    if (addr == UanAddress::GetBroadcast())
        return Mac48Address ("ff:ff:ff:ff:ff:ff");

    uint8_t key;
    addr.CopyTo(&key);
    std::unordered_map<uint8_t,Mac48Address>::const_iterator got = getMap.find (key);

    if ( got != getMap.end() )
      return got->second;
    
    Mac48Address translated = Mac48Address::Allocate();
    getMap[key] = translated;

    char buff[6];
    translated.CopyTo((uint8_t*) buff);
    std::string trK = buff;
    storeMap[trK] = addr;
  }

  void AddressTranslator::remove(Mac48Address addr){
    char buff[6];
    addr.CopyTo((uint8_t*) buff);
    std::string key = buff;
    std::unordered_map<std::string,UanAddress>::const_iterator got = storeMap.find (key);
    if ( got != storeMap.end() ){
      storeMap.erase(key);

      uint8_t translated;
      got->second.CopyTo(&translated);
      getMap.erase(translated);
    }
  }

} // namespace ns3
