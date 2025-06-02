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

#ifndef GALOIS_FIELD_H
#define GALOIS_FIELD_H

#include "ns3/object.h"
#include <vector>

namespace ns3 {

/**
 * \ingroup network-coding
 * \brief Galois Field (2^8) arithmetic implementation for network coding
 *
 * This class implements operations in the finite field GF(2^8) which is used
 * for network coding operations. Galois field arithmetic is different from
 * standard integer arithmetic and is essential for ensuring that coded packets
 * can be properly decoded.
 */
class GaloisField : public Object
{
public:
  /**
   * \brief Get the TypeId
   * \return the TypeId for this class
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Default constructor for GaloisField
   *
   * Initializes the lookup tables used for Galois field operations
   */
  GaloisField ();

  /**
   * \brief Destructor for GaloisField
   */
  virtual ~GaloisField ();

  /**
   * \brief Add two elements in GF(2^8)
   * \param a First operand
   * \param b Second operand
   * \return a + b in GF(2^8)
   *
   * Addition in GF(2^8) is performed using XOR
   */
  uint8_t Add (uint8_t a, uint8_t b);

  /**
   * \brief Subtract two elements in GF(2^8)
   * \param a First operand
   * \param b Second operand
   * \return a - b in GF(2^8)
   *
   * Subtraction in GF(2^8) is the same as addition (XOR)
   */
  uint8_t Subtract (uint8_t a, uint8_t b);

  /**
   * \brief Multiply two elements in GF(2^8)
   * \param a First operand
   * \param b Second operand
   * \return a * b in GF(2^8)
   *
   * Multiplication is performed using logarithm tables to improve performance
   */
  uint8_t Multiply (uint8_t a, uint8_t b);

  /**
   * \brief Divide two elements in GF(2^8)
   * \param a First operand
   * \param b Second operand
   * \return a / b in GF(2^8)
   *
   * Division is performed using logarithm tables
   */
  uint8_t Divide (uint8_t a, uint8_t b);

  /**
   * \brief Find the multiplicative inverse of an element in GF(2^8)
   * \param a Element to invert
   * \return Multiplicative inverse of a in GF(2^8)
   */
  uint8_t Inverse (uint8_t a);

private:
  /**
   * \brief Initialize lookup tables for Galois field operations
   */
  void InitTables (void);

  /**
   * \brief Field size constant (2^8 = 256)
   */
  static const uint16_t FIELD_SIZE = 256; // GF(2^8)

  /**
   * \brief Logarithm lookup table
   */
  std::vector<uint8_t> m_logTable;

  /**
   * \brief Exponentiation lookup table
   */
  std::vector<uint8_t> m_expTable;
};

} // namespace ns3

#endif /* GALOIS_FIELD_H */