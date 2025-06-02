/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
  /*
  * Copyright (c) 2023
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
  *
  * Author: Your Name <your.email@example.com>
  */

  #include "galois-field.h"
  #include "ns3/log.h"

  namespace ns3 {

  NS_LOG_COMPONENT_DEFINE ("GaloisField");
  NS_OBJECT_ENSURE_REGISTERED (GaloisField);

  TypeId 
  GaloisField::GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::GaloisField")
      .SetParent<Object> ()
      .SetGroupName ("NetworkCoding")
      .AddConstructor<GaloisField> ()
    ;
    return tid;
  }

  GaloisField::GaloisField ()
    : m_logTable(FIELD_SIZE, 0),
      m_expTable(2 * FIELD_SIZE, 0)
  {
    NS_LOG_FUNCTION (this);
    InitTables ();
  }

  GaloisField::~GaloisField ()
  {
    NS_LOG_FUNCTION (this);
  }

  void 
  GaloisField::InitTables (void)
  {
    NS_LOG_FUNCTION (this);
    
    // Using primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11d)
    const uint16_t poly = 0x11d;
    uint16_t x = 1;
    
    // Generate the exponential table
    for (uint16_t i = 0; i < FIELD_SIZE - 1; i++) {
      m_expTable[i] = static_cast<uint8_t>(x);
      
      // Find log by inverse lookup
      m_logTable[x] = i;
      
      // Multiply by the primitive element
      x = x << 1;
      if (x & 0x100) {
        x = (x ^ poly) & 0xff;
      }
    }
    
    // Set special case for log[0]
    m_logTable[0] = 0; // Log of 0 is undefined, but we set it to 0
    
    // Extend the exp table for easier computation
    for (uint16_t i = FIELD_SIZE - 1; i < 2 * FIELD_SIZE - 1; i++) {
      m_expTable[i] = m_expTable[i - (FIELD_SIZE - 1)];
    }
    
    // Log debug info only once during initialization
    static bool debugPrinted = false;
    if (!debugPrinted) {
      std::cout << "DEBUG: GF Exp table initialized:" << std::endl;
      for (int i = 0; i < 10; i++) {
        std::cout << "  Exp[" << i << "] = " << (int)m_expTable[i] << std::endl;
      }
      
      std::cout << "DEBUG: GF Log table initialized:" << std::endl;
      for (int i = 0; i < 10; i++) {
        std::cout << "  Log[" << i << "] = " << (int)m_logTable[i] << std::endl;
      }
      debugPrinted = true;
    }
  }

  uint8_t 
  GaloisField::Add (uint8_t a, uint8_t b)
  {
    // Addition in GF(2^8) is simply XOR
    return a ^ b;
  }

  uint8_t 
  GaloisField::Subtract (uint8_t a, uint8_t b)
  {
    // Subtraction in GF(2^8) is the same as addition
    return Add(a, b);
  }

  uint8_t 
  GaloisField::Multiply (uint8_t a, uint8_t b)
  {
    // Special case for 0
    if (a == 0 || b == 0) {
      return 0;
    }
    
    // Use log/exp for multiplication
    // a * b = exp(log(a) + log(b))
    uint16_t sum = m_logTable[a] + m_logTable[b];
    
    // Take the modulo for the finite field
    if (sum >= FIELD_SIZE - 1) {
      sum = sum % (FIELD_SIZE - 1);
    }
    
    return m_expTable[sum];
  }

  uint8_t 
  GaloisField::Divide (uint8_t a, uint8_t b)
  {
    if (b == 0) {
      NS_LOG_ERROR ("Division by zero in Galois Field");
      return 0;
    }
    
    if (a == 0) {
      return 0;
    }
    
    // a / b = exp(log(a) - log(b))
    int16_t diff = (int16_t)m_logTable[a] - (int16_t)m_logTable[b];
    
    // Ensure the result is non-negative
    if (diff < 0) {
      diff += (FIELD_SIZE - 1);
    }
    
    return m_expTable[diff];
  }

  uint8_t 
  GaloisField::Inverse (uint8_t a)
  {
    if (a == 0) {
      NS_LOG_ERROR ("Cannot compute inverse of zero in Galois Field");
      return 0;
    }
    
    // inv(a) = exp(-log(a)) = exp((FIELD_SIZE-1) - log(a))
    uint16_t index = (FIELD_SIZE - 1) - m_logTable[a];
    
    return m_expTable[index];
  }

  } // namespace ns3